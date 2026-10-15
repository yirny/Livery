// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State        - one mutex-guarded struct: request, name buffer, category ticks, status
//   Game thread  - Open / Close / SetError (also freeze and unfreeze the game)
//   Input        - QueueInput translates engine events; FeedInput hands them and
//                  the game's cursor position to ImGui
//   Draw         - the window itself
#include "PCH.h"

#include "CaptureWindow.h"

#include "Hooks.h"
#include "Theme.h"

#include <imgui.h>
#include <imgui_internal.h>  // ClosePopupsExceptModals

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>

namespace
{
	constexpr std::size_t kNameCapacity = 64;  // bytes, UTF-8, including the terminator

	enum class Status
	{
		kClosed,
		kEditing,
		kSaving  // Save pressed; waiting for onSave to Close() or SetError()
	};

	struct InputRecord
	{
		enum class Kind
		{
			kKey,
			kMouseButton,
			kMouseWheel,
			kChar
		};
		Kind  kind;
		int   code;  // ImGuiKey / mouse button / character
		bool  down;
		float wheel;  // wheel steps
	};

	// --- State ----------------------------------------------------------------

	struct State
	{
		std::mutex                      mutex;
		Status                          status = Status::kClosed;
		CaptureWindow::Request          request;
		std::array<char, kNameCapacity> name{};
		std::unique_ptr<bool[]>         pieceTicked;  // one per request.pieces; bool[] because ImGui::Checkbox needs bool*
		std::unique_ptr<bool[]>         ticked;       // one per request.categories
		std::string                     error;
		bool                            focusNameField = false;
		std::vector<InputRecord>        pending;
		bool                            resetImGui = false;  // set by Open, handled on the next frame
	};

	State& GetState()
	{
		static State state;
		return state;
	}

	std::atomic_bool g_open = false;  // mirrors status != kClosed for lock-free IsOpen()

	void SetGameFrozen(bool a_frozen)
	{
		if (auto* main = RE::Main::GetSingleton()) {
			main->GetRuntimeData().freezeTime = a_frozen;
		}
	}

	std::string Trimmed(const char* a_text)
	{
		std::string text = a_text;
		const auto  first = text.find_first_not_of(" \t");
		if (first == std::string::npos) {
			return {};
		}
		const auto last = text.find_last_not_of(" \t");
		return text.substr(first, last - first + 1);
	}

	// --- Input ----------------------------------------------------------------

	// DirectInput scan code -> ImGuiKey, for the keys a text field needs.
	// Printable characters arrive as CharEvents, so letters are only mapped
	// where ImGui uses them as shortcuts (select all, copy, paste, undo).
	ImGuiKey ToImGuiKey(std::uint32_t a_scanCode)
	{
		using Key = RE::BSKeyboardDevice::Key;
		switch (a_scanCode) {
		case Key::kEscape:
			return ImGuiKey_Escape;
		case Key::kEnter:
			return ImGuiKey_Enter;
		case Key::kKP_Enter:
			return ImGuiKey_KeypadEnter;
		case Key::kTab:
			return ImGuiKey_Tab;
		case Key::kBackspace:
			return ImGuiKey_Backspace;
		case Key::kDelete:
			return ImGuiKey_Delete;
		case Key::kLeft:
			return ImGuiKey_LeftArrow;
		case Key::kRight:
			return ImGuiKey_RightArrow;
		case Key::kUp:
			return ImGuiKey_UpArrow;
		case Key::kDown:
			return ImGuiKey_DownArrow;
		case Key::kHome:
			return ImGuiKey_Home;
		case Key::kEnd:
			return ImGuiKey_End;
		case Key::kSpacebar:
			return ImGuiKey_Space;
		case Key::kLeftShift:
		case Key::kRightShift:
			return ImGuiMod_Shift;
		case Key::kLeftControl:
		case Key::kRightControl:
			return ImGuiMod_Ctrl;
		case Key::kLeftAlt:
		case Key::kRightAlt:
			return ImGuiMod_Alt;
		case Key::kA:
			return ImGuiKey_A;
		case Key::kC:
			return ImGuiKey_C;
		case Key::kV:
			return ImGuiKey_V;
		case Key::kX:
			return ImGuiKey_X;
		case Key::kY:
			return ImGuiKey_Y;
		case Key::kZ:
			return ImGuiKey_Z;
		default:
			return ImGuiKey_None;
		}
	}

	void RecordButton(std::vector<InputRecord>& a_out, const RE::ButtonEvent& a_button)
	{
		using MouseKey = RE::BSWin32MouseDevice::Key;
		const auto code = a_button.GetIDCode();

		switch (a_button.GetDevice()) {
		case RE::INPUT_DEVICE::kKeyboard:
			if (const auto key = ToImGuiKey(code); key != ImGuiKey_None && (a_button.IsDown() || a_button.IsUp())) {
				a_out.push_back({ InputRecord::Kind::kKey, key, a_button.IsDown(), 0.0f });
			}
			break;
		case RE::INPUT_DEVICE::kMouse:
			if (code == MouseKey::kWheelUp || code == MouseKey::kWheelDown) {
				if (a_button.IsDown()) {
					a_out.push_back({ InputRecord::Kind::kMouseWheel, 0, true, code == MouseKey::kWheelUp ? 1.0f : -1.0f });
				}
			} else if (code <= MouseKey::kMiddleButton && (a_button.IsDown() || !a_button.IsPressed())) {
				// Any zero value is a release: IsUp() also needs a held time above
				// zero, which a quick click may not have. ImGui drops repeated states.
				a_out.push_back({ InputRecord::Kind::kMouseButton, static_cast<int>(code), a_button.IsDown(), 0.0f });
			}
			break;
		default:
			break;
		}
	}

	// Contract: the cursor position is queued just before these (FeedInput):
	// ImGui stops moving the cursor after a button change in a frame, so a
	// position queued after a click would put the click elsewhere.
	void FlushInput(State& a_state, ImGuiIO& a_io)
	{
		for (const auto& record : a_state.pending) {
			switch (record.kind) {
			case InputRecord::Kind::kKey:
				a_io.AddKeyEvent(static_cast<ImGuiKey>(record.code), record.down);
				break;
			case InputRecord::Kind::kMouseButton:
				a_io.AddMouseButtonEvent(record.code, record.down);
				break;
			case InputRecord::Kind::kMouseWheel:
				a_io.AddMouseWheelEvent(0.0f, record.wheel);
				break;
			case InputRecord::Kind::kChar:
				a_io.AddInputCharacter(static_cast<unsigned int>(record.code));
				break;
			}
		}
		a_state.pending.clear();
	}

	// No ImGui frame runs while the window is closed, so ImGui still holds what
	// it had at closing time: queued events, held keys and buttons, open popups.
	// Contract: render thread, inside the frame (after ImGui::NewFrame).
	void ResetImGuiState(ImGuiIO& a_io)
	{
		a_io.ClearEventsQueue();
		a_io.ClearInputKeys();
		a_io.ClearInputMouse();
		ImGui::ClosePopupsExceptModals();
	}

	// --- Draw -----------------------------------------------------------------

	std::size_t CountTickedPieces(const State& a_state)
	{
		std::size_t count = 0;
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			count += a_state.pieceTicked[i] ? 1 : 0;
		}
		return count;
	}

	// Right column: a checkbox per worn piece; unticked pieces are left out of
	// the outfit. The list is exactly as tall as its rows so it does not scroll,
	// capped so a heavily dressed NPC cannot push the window off screen.
	void DrawPieceList(State& a_state)
	{
		constexpr float kMaxHeightRatio = 0.6f;  // of the screen height

		const auto& pieces = a_state.request.pieces;
		const auto& style = ImGui::GetStyle();
		Theme::LabelText(std::format("{} of {} piece(s) selected", CountTickedPieces(a_state), pieces.size()).c_str());
		const float rowsHeight = static_cast<float>(pieces.size()) * ImGui::GetFrameHeightWithSpacing() - style.ItemSpacing.y;
		const float fullHeight = rowsHeight + (style.WindowPadding.y + style.ChildBorderSize) * 2.0f;
		const float listHeight = (std::min)(fullHeight, ImGui::GetIO().DisplaySize.y * kMaxHeightRatio);
		if (ImGui::BeginChild("pieces", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
			for (std::size_t i = 0; i < pieces.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				ImGui::Checkbox(pieces[i].name.c_str(), &a_state.pieceTicked[i]);
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}

	std::string CategoryPreview(const State& a_state)
	{
		std::string preview;
		for (std::size_t i = 0; i < a_state.request.categories.size(); ++i) {
			if (a_state.ticked[i]) {
				preview += preview.empty() ? "" : ", ";
				preview += a_state.request.categories[i].name;
			}
		}
		return preview.empty() ? "None" : preview;
	}

	// One full-width option row: hover fill, a highlighted fill and accent text
	// with a check mark on the right when ticked. An InvisibleButton does not
	// close the combo popup, so the user can tick several rows in one go.
	// The hit area also covers half the gap above and below the fill, so rows
	// stacked with no item spacing leave no dead strip between them.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	void DrawCategoryRow(const char* a_label, bool& a_ticked)
	{
		const auto&  style = ImGui::GetStyle();
		const float  halfGap = style.ItemInnerSpacing.y * 0.5f;
		const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + halfGap * 2.0f);
		const ImVec2 hitTopLeft = ImGui::GetCursorScreenPos();
		if (ImGui::InvisibleButton(a_label, size)) {
			a_ticked = !a_ticked;
		}
		const bool hovered = ImGui::IsItemHovered();

		const ImVec2 topLeft(hitTopLeft.x, hitTopLeft.y + halfGap);
		const ImVec2 bottomRight(hitTopLeft.x + size.x, hitTopLeft.y + size.y - halfGap);
		auto*        draw = ImGui::GetWindowDrawList();
		if (a_ticked || hovered) {
			draw->AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32(a_ticked ? ImGuiCol_Header : ImGuiCol_FrameBgHovered), style.FrameRounding);
		}
		const ImU32  textColour = ImGui::GetColorU32(a_ticked ? ImGuiCol_CheckMark : ImGuiCol_Text);
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const float  midY = (topLeft.y + bottomRight.y) * 0.5f;
		draw->AddText(ImVec2(topLeft.x + style.FramePadding.x, midY - text.y * 0.5f), textColour, a_label);
		if (a_ticked) {
			const float  mark = ImGui::GetFontSize() * 0.3f;  // half width of the check mark
			const float  right = bottomRight.x - style.FramePadding.x;
			const ImVec2 points[] = { { right - mark * 2.0f, midY }, { right - mark * 1.2f, midY + mark * 0.8f }, { right, midY - mark * 0.8f } };
			draw->AddPolyline(points, 3, textColour, ImDrawFlags_None, (std::max)(1.0f, ImGui::GetFontSize() * 0.08f));
		}
	}

	// Multi-select dropdown: a combo whose popup lists every category as a row.
	void DrawCategoryField(State& a_state)
	{
		Theme::LabelText("Categories");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##categories", CategoryPreview(a_state).c_str(), ImGuiComboFlags_HeightLargest)) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));  // rows carry their own gap
			const auto& options = a_state.request.categories;
			for (std::size_t i = 0; i < options.size(); ++i) {
				if (i == 0 || options[i].group != options[i - 1].group) {
					if (i > 0) {
						ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemInnerSpacing.y));  // small gap before each later heading
					}
					ImGui::TextDisabled("%s", options[i].group.c_str());
				}
				ImGui::PushID(static_cast<int>(i));
				DrawCategoryRow(options[i].name.c_str(), a_state.ticked[i]);
				ImGui::PopID();
			}
			ImGui::PopStyleVar();
			ImGui::EndCombo();
		}
	}

	// Returns true when the user asked to save with a valid name.
	bool DrawNameField(State& a_state)
	{
		Theme::LabelText("Outfit name");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (a_state.focusNameField) {
			ImGui::SetKeyboardFocusHere();
			a_state.focusNameField = false;
		}
		// The hint line carries no spacing of its own, so the name sits only one text line above Categories.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
		const bool enter = ImGui::InputText("##name", a_state.name.data(), a_state.name.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		const bool valid = !Trimmed(a_state.name.data()).empty();
		if (!valid) {
			ImGui::TextDisabled("Enter a name.");
		} else if (CountTickedPieces(a_state) == 0) {
			ImGui::TextDisabled("Tick at least one piece.");
		} else if (!a_state.error.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::TextWrapped("%s", a_state.error.c_str());
			ImGui::PopStyleColor();
		} else {
			ImGui::NewLine();
		}
		ImGui::PopStyleVar();
		return enter && valid;
	}

	enum class FormAction
	{
		kNone,
		kSave,
		kCancel
	};

	// Save / Cancel centred under a rule that spans the window.
	// a_save is true when the name field already asked to save.
	FormAction DrawButtons(State& a_state, bool a_save)
	{
		const auto& style = ImGui::GetStyle();
		ImGui::Separator();
		const float width = Theme::ButtonWidth("Save") + style.ItemSpacing.x + Theme::ButtonWidth("Cancel");
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
		const bool valid = !Trimmed(a_state.name.data()).empty() && CountTickedPieces(a_state) > 0;
		ImGui::BeginDisabled(!valid);
		const bool save = Theme::Button("Save") || a_save;
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Saving...");
			return FormAction::kNone;
		}
		if (save && valid) {
			return FormAction::kSave;
		}
		return cancel ? FormAction::kCancel : FormAction::kNone;
	}

	void RequestSave(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		const auto               name = Trimmed(a_state.name.data());
		std::vector<std::size_t> pieces;
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			if (a_state.pieceTicked[i]) {
				pieces.push_back(i);
			}
		}
		std::vector<std::string> categories;
		for (std::size_t i = 0; i < a_state.request.categories.size(); ++i) {
			if (a_state.ticked[i]) {
				categories.push_back(a_state.request.categories[i].name);
			}
		}
		auto onSave = a_state.request.onSave;
		SKSE::GetTaskInterface()->AddTask([onSave, name, pieces, categories] { onSave(name, pieces, categories); });
	}

	void RequestCancel()
	{
		SKSE::GetTaskInterface()->AddTask([] { CaptureWindow::Close(); });
	}
}

namespace CaptureWindow
{
	// --- Game thread ----------------------------------------------------------

	void Open(Request a_request)
	{
		auto& state = GetState();
		{
			const std::scoped_lock lock(state.mutex);
			state.request = std::move(a_request);
			state.name.fill('\0');
			std::strncpy(state.name.data(), state.request.suggestedName.c_str(), kNameCapacity - 1);
			state.pieceTicked = std::make_unique<bool[]>(state.request.pieces.size());
			for (std::size_t i = 0; i < state.request.pieces.size(); ++i) {
				state.pieceTicked[i] = state.request.pieces[i].selected;
			}
			state.ticked = std::make_unique<bool[]>(state.request.categories.size());
			for (std::size_t i = 0; i < state.request.categories.size(); ++i) {
				state.ticked[i] = state.request.categories[i].selected;
			}
			state.error.clear();
			state.pending.clear();
			state.focusNameField = true;
			state.status = Status::kEditing;
			state.resetImGui = true;
		}
		g_open = true;
		SetGameFrozen(true);
		Hooks::ShowMenu();
	}

	void Close()
	{
		auto& state = GetState();
		{
			const std::scoped_lock lock(state.mutex);
			if (state.status == Status::kClosed) {
				return;
			}
			state.status = Status::kClosed;
			state.request = {};
			state.ticked.reset();
			state.pending.clear();
		}
		g_open = false;  // before HideMenu: the menu's hide handler tells our hide from the game's by this
		SetGameFrozen(false);
		Hooks::HideMenu();
	}

	void SetError(const std::string& a_message)
	{
		auto&                  state = GetState();
		const std::scoped_lock lock(state.mutex);
		if (state.status == Status::kSaving) {
			state.status = Status::kEditing;
			state.error = a_message;
			state.focusNameField = true;
		}
	}

	bool IsOpen()
	{
		return g_open;
	}

	// --- Input (game thread) --------------------------------------------------

	void QueueInput(RE::InputEvent* const* a_events)
	{
		auto&                  state = GetState();
		const std::scoped_lock lock(state.mutex);
		for (auto* event = *a_events; event; event = event->next) {
			if (const auto* button = event->AsButtonEvent()) {
				RecordButton(state.pending, *button);
			} else if (const auto* chr = event->AsCharEvent()) {
				// ASCII only: the game's char events carry the ANSI code page.
				if (chr->keyCode >= 0x20 && chr->keyCode < 0x7F) {
					state.pending.push_back({ InputRecord::Kind::kChar, static_cast<int>(chr->keyCode), true, 0.0f });
				}
			}
		}
	}

	// --- Render thread --------------------------------------------------------

	// The game's cursor is queued after the OS cursor that ImGui_ImplWin32_NewFrame
	// queued, so it wins, and before the buttons, so clicks land where the game
	// draws its cursor.
	void FeedInput()
	{
		auto&                  state = GetState();
		const std::scoped_lock lock(state.mutex);
		if (state.status == Status::kClosed) {
			return;
		}

		auto& io = ImGui::GetIO();
		if (auto* cursor = RE::MenuCursor::GetSingleton()) {
			const auto& position = cursor->GetRuntimeData();
			io.AddMousePosEvent(position.cursorPosX, position.cursorPosY);
		}
		FlushInput(state, io);
	}

	void Draw()
	{
		auto&                  state = GetState();
		const std::scoped_lock lock(state.mutex);
		if (state.status == Status::kClosed) {
			return;
		}

		auto& io = ImGui::GetIO();
		if (state.resetImGui) {
			ResetImGuiState(io);
			state.resetImGui = false;
		}

		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		constexpr float kWindowWidthRatio = 0.45f;  // of the screen width
		constexpr float kFormWidthRatio = 0.45f;    // left column, of the table width
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * kWindowWidthRatio, 0.0f), ImGuiCond_Appearing);
		auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
		if (!Theme::HasTitleBar()) {
			flags |= ImGuiWindowFlags_NoTitleBar;
		}
		if (!ImGui::Begin("Capturing Outfit for Tailor", nullptr, flags)) {
			ImGui::End();
			return;
		}
		Theme::DecorateWindow();

		Theme::HeadingText(state.request.actorName.c_str());

		constexpr float kColumnGap = 24.0f;  // pixels at 1080p, between the form and the piece list

		bool save = false;
		ImGui::BeginDisabled(state.status != Status::kEditing);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(kColumnGap * 0.5f * Theme::Scale(), ImGui::GetStyle().CellPadding.y));
		if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("form", ImGuiTableColumnFlags_WidthStretch, kFormWidthRatio);
			ImGui::TableSetupColumn("pieces", ImGuiTableColumnFlags_WidthStretch, 1.0f - kFormWidthRatio);
			ImGui::TableNextColumn();
			save = DrawNameField(state);
			DrawCategoryField(state);
			ImGui::TableNextColumn();
			// A table hands the next cell the text baseline of the previous cell's last
			// line (the combo's frame padding), which would push this label down.
			ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.0f;
			DrawPieceList(state);
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		const auto action = DrawButtons(state, save);
		ImGui::EndDisabled();

		if (action == FormAction::kSave) {
			RequestSave(state);
		} else if (action == FormAction::kCancel) {
			RequestCancel();
		}
		ImGui::End();
	}
}
