// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State        - one mutex-guarded struct: request, name buffer, category ticks, status
//   Game thread  - Open / Close / SetError (also freeze and unfreeze the game)
//   Input        - QueueInput translates engine events; Draw feeds them to ImGui
//   Draw         - the window itself
#include "PCH.h"

#include "CaptureWindow.h"

#include "Theme.h"

#include <imgui.h>

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
			kMouseMove,
			kMouseWheel,
			kChar
		};
		Kind  kind;
		int   code;  // ImGuiKey / mouse button / character
		bool  down;
		float x;  // mouse delta x, or wheel steps
		float y;
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
		ImVec2                          mouse{ 0.0f, 0.0f };
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
				a_out.push_back({ InputRecord::Kind::kKey, key, a_button.IsDown(), 0.0f, 0.0f });
			}
			break;
		case RE::INPUT_DEVICE::kMouse:
			if (code == MouseKey::kWheelUp || code == MouseKey::kWheelDown) {
				if (a_button.IsDown()) {
					a_out.push_back({ InputRecord::Kind::kMouseWheel, 0, true, 0.0f, code == MouseKey::kWheelUp ? 1.0f : -1.0f });
				}
			} else if (code <= MouseKey::kMiddleButton && (a_button.IsDown() || a_button.IsUp())) {
				a_out.push_back({ InputRecord::Kind::kMouseButton, static_cast<int>(code), a_button.IsDown(), 0.0f, 0.0f });
			}
			break;
		default:
			break;
		}
	}

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
				a_io.AddMouseWheelEvent(0.0f, record.y);
				break;
			case InputRecord::Kind::kMouseMove:
				a_state.mouse.x = std::clamp(a_state.mouse.x + record.x, 0.0f, a_io.DisplaySize.x - 1.0f);
				a_state.mouse.y = std::clamp(a_state.mouse.y + record.y, 0.0f, a_io.DisplaySize.y - 1.0f);
				a_io.AddMousePosEvent(a_state.mouse.x, a_state.mouse.y);
				break;
			case InputRecord::Kind::kChar:
				a_io.AddInputCharacter(static_cast<unsigned int>(record.code));
				break;
			}
		}
		a_state.pending.clear();
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

	// Checkbox per worn piece; unticked pieces are left out of the outfit.
	void DrawPieceList(State& a_state)
	{
		const auto& pieces = a_state.request.pieces;
		Theme::HeadingText(a_state.request.actorName.c_str());
		Theme::LabelText(std::format("{} of {} piece(s) selected", CountTickedPieces(a_state), pieces.size()).c_str());
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
		const float listHeight = rowHeight * static_cast<float>(std::min<std::size_t>(pieces.size(), 8)) + rowHeight * 0.5f;
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

	// Multi-select dropdown: checkboxes inside a combo, which keep the popup
	// open while the user ticks several categories.
	void DrawCategoryField(State& a_state)
	{
		Theme::LabelText("Categories");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##categories", CategoryPreview(a_state).c_str(), ImGuiComboFlags_HeightLargest)) {
			const auto& options = a_state.request.categories;
			for (std::size_t i = 0; i < options.size(); ++i) {
				if (i == 0 || options[i].group != options[i - 1].group) {
					ImGui::TextDisabled("%s", options[i].group.c_str());
				}
				ImGui::Checkbox(options[i].name.c_str(), &a_state.ticked[i]);
			}
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
		const bool enter = ImGui::InputText("##name", a_state.name.data(), a_state.name.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		const bool valid = !Trimmed(a_state.name.data()).empty();
		if (!valid) {
			ImGui::TextDisabled("Enter a name.");
		} else if (CountTickedPieces(a_state) == 0) {
			ImGui::TextDisabled("Tick at least one piece.");
		} else if (!a_state.error.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", a_state.error.c_str());
		} else {
			ImGui::NewLine();
		}
		return enter && valid;
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
			state.ticked = std::make_unique<bool[]>(state.request.categories.size());  // all false
			state.error.clear();
			state.pending.clear();
			state.focusNameField = true;
			state.status = Status::kEditing;
			state.mouse = ImVec2(-1.0f, -1.0f);  // centred on the first frame
		}
		g_open = true;
		SetGameFrozen(true);
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
		g_open = false;
		SetGameFrozen(false);
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
			} else if (const auto* move = event->AsMouseMoveEvent()) {
				state.pending.push_back({ InputRecord::Kind::kMouseMove, 0, false, static_cast<float>(move->mouseInputX), static_cast<float>(move->mouseInputY) });
			} else if (const auto* chr = event->AsCharEvent()) {
				// ASCII only: the game's char events carry the ANSI code page.
				if (chr->keyCode >= 0x20 && chr->keyCode < 0x7F) {
					state.pending.push_back({ InputRecord::Kind::kChar, static_cast<int>(chr->keyCode), true, 0.0f, 0.0f });
				}
			}
		}
	}

	// --- Render thread --------------------------------------------------------

	void Draw()
	{
		auto&                  state = GetState();
		const std::scoped_lock lock(state.mutex);
		if (state.status == Status::kClosed) {
			return;
		}

		auto& io = ImGui::GetIO();
		if (state.mouse.x < 0.0f) {
			state.mouse = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
			io.AddMousePosEvent(state.mouse.x, state.mouse.y);
		}
		FlushInput(state, io);

		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.3f, 0.0f), ImGuiCond_Appearing);
		auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
		if (!Theme::HasTitleBar()) {
			flags |= ImGuiWindowFlags_NoTitleBar;
		}
		if (!ImGui::Begin("Tailor Capture", nullptr, flags)) {
			ImGui::End();
			return;
		}
		Theme::DecorateWindow();

		const bool editing = state.status == Status::kEditing;
		ImGui::BeginDisabled(!editing);
		DrawPieceList(state);
		ImGui::Separator();

		bool save = DrawNameField(state);
		DrawCategoryField(state);
		ImGui::Spacing();
		const bool valid = !Trimmed(state.name.data()).empty() && CountTickedPieces(state) > 0;
		ImGui::BeginDisabled(!valid);
		save = Theme::Button("Save") || save;
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		ImGui::EndDisabled();

		if (editing && save && valid) {
			RequestSave(state);
		} else if (editing && cancel) {
			RequestCancel();
		} else if (!editing) {
			ImGui::TextDisabled("Saving...");
		}
		ImGui::End();
	}
}
