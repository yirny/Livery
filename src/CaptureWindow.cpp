// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State        - one mutex-guarded struct: request, name buffer, category ticks, status
//   Game thread  - Open / Close / SetError (also freeze and unfreeze the game)
//   Input        - QueueInput translates engine events; FeedInput hands them and
//                  the game's cursor position to ImGui
//   Draw         - page switching, shared list and button-row helpers
//   Pieces page  - inventory pieces (first page), each tick handed to onPieceToggled;
//                  Create New / Update Existing / Cancel
//   Name page    - name, categories and gender of the new outfit; Create / Back
//   Update page  - library outfits, the chosen one's categories (its own ones
//                  locked, more can be ticked) and pieces; Update opens the
//                  confirm popup, whose Confirm hands over to onUpdate
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
#include <initializer_list>
#include <memory>
#include <mutex>

namespace
{
	constexpr std::size_t kNameCapacity = 64;                 // bytes, UTF-8, including the terminator
	constexpr float       kMaxListHeightRatio = 0.6f;         // of the screen height, for every scrolling list
	constexpr const char* kConfirmPopup = "Replace pieces?";  // ID shared by OpenPopup and BeginPopupModal

	enum class Status
	{
		kClosed,
		kEditing,
		kSaving  // Create or Confirm pressed; waiting for onSave / onUpdate to Close() or SetError()
	};

	enum class Page
	{
		kPieces,
		kName,
		kUpdate
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
		std::unique_ptr<bool[]>         pieceTicked;   // one per request.pieces; bool[] because ImGui::Checkbox needs bool*
		std::unique_ptr<bool[]>         ticked;        // one per request.categories
		std::unique_ptr<bool[]>         updateTicked;  // one per request.categories, for the selected outfit
		std::unique_ptr<bool[]>         updateLocked;  // one per request.categories: the selected outfit is already in it
		int                             gender = -1;   // of the new outfit: -1 unisex, 0 male, 1 female
		std::string                     error;
		bool                            focusNameField = false;
		std::vector<InputRecord>        pending;
		bool                            resetImGui = false;  // set by Open, handled on the next frame
		Page                            page = Page::kPieces;
		int                             selectedOutfit = -1;  // index into request.existingOutfits, -1 for none
		int                             recentreFrames = 0;   // frames left to pin the window to the screen centre
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
	// it had at closing time: queued events, held keys and buttons, open popups
	// (the confirm popup included).
	// Contract: render thread, inside the frame (after ImGui::NewFrame).
	void ResetImGuiState(ImGuiIO& a_io)
	{
		a_io.ClearEventsQueue();
		a_io.ClearInputKeys();
		a_io.ClearInputMouse();
		// ClosePopupToLevel needs at least one open popup.
		if (ImGui::GetCurrentContext()->OpenPopupStack.Size > 0) {
			ImGui::ClosePopupToLevel(0, false);
		}
	}

	// --- Draw -----------------------------------------------------------------

	// The window is pinned to the screen centre for a few frames, as the pages
	// differ in size and an auto-resized window fits new content one frame late.
	void GoToPage(State& a_state, Page a_page)
	{
		constexpr int kRecentreFrames = 2;  // the frame that draws the new page and the one that has fitted it

		a_state.page = a_page;
		a_state.error.clear();
		a_state.focusNameField = a_page == Page::kName;
		a_state.recentreFrames = kRecentreFrames;
	}

	std::vector<std::size_t> TickedPieces(const State& a_state)
	{
		std::vector<std::size_t> ticked;
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			if (a_state.pieceTicked[i]) {
				ticked.push_back(i);
			}
		}
		return ticked;
	}

	std::size_t CountTickedPieces(const State& a_state)
	{
		return TickedPieces(a_state).size();
	}

	// Height of a bordered list exactly as tall as its rows, so it does not
	// scroll, capped so a long list cannot push the window off screen.
	// An empty list keeps one row: a child of height zero would fill the window.
	float ListHeight(std::size_t a_rows, float a_rowHeight)
	{
		const auto& style = ImGui::GetStyle();
		const float rowsHeight = static_cast<float>((std::max)(a_rows, std::size_t{ 1 })) * a_rowHeight - style.ItemSpacing.y;
		const float fullHeight = rowsHeight + (style.WindowPadding.y + style.ChildBorderSize) * 2.0f;
		return (std::min)(fullHeight, ImGui::GetIO().DisplaySize.y * kMaxListHeightRatio);
	}

	void DrawError(const std::string& a_message)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", a_message.c_str());
		ImGui::PopStyleColor();
	}

	// A gap, then the cursor placed so a row of buttons with these labels
	// (Theme::Button, one item spacing apart) is centred.
	void BeginButtonRow(std::initializer_list<const char*> a_labels)
	{
		constexpr float kGap = 17.0f;  // pixels at 1080p, on top of one extra item spacing

		const auto& style = ImGui::GetStyle();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.ItemSpacing.y + kGap * Theme::Scale());
		float width = style.ItemSpacing.x * static_cast<float>(a_labels.size() - 1);
		for (const auto* label : a_labels) {
			width += Theme::ButtonWidth(label);
		}
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
	}

	// A table hands the next cell the text baseline of the previous cell's last
	// line (a frame's padding), which would push the cell's first label down.
	void ResetCellBaseline()
	{
		ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.0f;
	}

	// The Update page's two-column layout. When it returns true, fill the
	// cells and call EndColumns.
	bool BeginColumns()
	{
		constexpr float kColumnGap = 24.0f;       // pixels at 1080p, between the columns
		constexpr float kLeftWidthRatio = 0.45f;  // of the table width

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(kColumnGap * 0.5f * Theme::Scale(), ImGui::GetStyle().CellPadding.y));
		if (!ImGui::BeginTable("layout", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::PopStyleVar();
			return false;
		}
		ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, kLeftWidthRatio);
		ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.0f - kLeftWidthRatio);
		return true;
	}

	void EndColumns()
	{
		ImGui::EndTable();
		ImGui::PopStyleVar();
	}

	void RequestCancel()
	{
		SKSE::GetTaskInterface()->AddTask([] { CaptureWindow::Close(); });
	}

	// --- Pieces page ----------------------------------------------------------

	// A checkbox per inventory piece; unticked pieces are left out of the outfit.
	// Each click goes to onPieceToggled (the live preview) in its own SKSE task,
	// queued in click order.
	void DrawPieceList(State& a_state)
	{
		const auto& pieces = a_state.request.pieces;
		const auto& onToggled = a_state.request.onPieceToggled;
		Theme::LabelText(std::format("{} of {} piece(s) selected", CountTickedPieces(a_state), pieces.size()).c_str());
		const float listHeight = ListHeight(pieces.size(), ImGui::GetFrameHeightWithSpacing());
		if (ImGui::BeginChild("pieces", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
			for (std::size_t i = 0; i < pieces.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Checkbox(pieces[i].name.c_str(), &a_state.pieceTicked[i]) && onToggled) {
					SKSE::GetTaskInterface()->AddTask([onToggled, i, ticked = a_state.pieceTicked[i]] { onToggled(i, ticked); });
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		if (CountTickedPieces(a_state) == 0) {
			ImGui::TextDisabled("Tick at least one piece.");
		}
		if (!a_state.request.previewNote.empty()) {
			ImGui::TextDisabled("%s", a_state.request.previewNote.c_str());
		}
	}

	enum class PiecesAction
	{
		kNone,
		kNamePage,
		kUpdatePage,
		kCancel
	};

	// Create New / Update Existing / Cancel centred below a gap. Both pages
	// they open act on the ticked pieces, so both need at least one.
	PiecesAction DrawPiecesButtons(const State& a_state)
	{
		BeginButtonRow({ "Create New", "Update Existing", "Cancel" });
		const bool hasPieces = CountTickedPieces(a_state) > 0;
		ImGui::BeginDisabled(!hasPieces);
		const bool create = Theme::Button("Create New");
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool canUpdate = hasPieces && !a_state.request.existingOutfits.empty();
		ImGui::BeginDisabled(!canUpdate);
		const bool update = Theme::Button("Update Existing");
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

		if (create && hasPieces) {
			return PiecesAction::kNamePage;
		}
		if (update && canUpdate) {
			return PiecesAction::kUpdatePage;
		}
		return cancel ? PiecesAction::kCancel : PiecesAction::kNone;
	}

	void DrawPiecesPage(State& a_state)
	{
		DrawPieceList(a_state);
		switch (DrawPiecesButtons(a_state)) {
		case PiecesAction::kNamePage:
			GoToPage(a_state, Page::kName);
			break;
		case PiecesAction::kUpdatePage:
			GoToPage(a_state, Page::kUpdate);
			break;
		case PiecesAction::kCancel:
			RequestCancel();
			break;
		case PiecesAction::kNone:
			break;
		}
	}

	// --- Name page ------------------------------------------------------------

	std::string JoinNames(const std::vector<std::string>& a_names)
	{
		std::string joined;
		for (const auto& name : a_names) {
			joined += joined.empty() ? "" : ", ";
			joined += name;
		}
		return joined;
	}

	std::string CategoryPreview(const State& a_state, const bool* a_ticked)
	{
		std::vector<std::string> names;
		for (std::size_t i = 0; i < a_state.request.categories.size(); ++i) {
			if (a_ticked[i]) {
				names.push_back(a_state.request.categories[i].name);
			}
		}
		return names.empty() ? "None" : JoinNames(names);
	}

	// One full-width option row: hover fill, a highlighted fill and accent text
	// with a check mark on the right when ticked. An InvisibleButton does not
	// close the combo popup, so the user can tick several rows in one go.
	// The hit area also covers half the gap above and below the fill, so rows
	// stacked with no item spacing leave no dead strip between them.
	// A locked row is drawn greyed out (BeginDisabled scales every colour's
	// alpha) and ignores clicks.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	void DrawCategoryRow(const char* a_label, bool& a_ticked, bool a_locked)
	{
		ImGui::BeginDisabled(a_locked);
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
		ImGui::EndDisabled();
	}

	// Multi-select dropdown: a combo whose popup lists every category as a row.
	// a_ticked and a_locked (nullptr for none locked) hold one entry per
	// request.categories.
	void DrawCategoryField(const State& a_state, const char* a_id, bool* a_ticked, const bool* a_locked)
	{
		Theme::LabelText("Categories");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo(a_id, CategoryPreview(a_state, a_ticked).c_str(), ImGuiComboFlags_HeightLargest)) {
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
				DrawCategoryRow(options[i].name.c_str(), a_ticked[i], a_locked && a_locked[i]);
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
		// The hint line sits right under the field and only a small gap above Categories.
		constexpr float kHintGap = 6.0f;  // pixels at 1080p, below the hint line
		const float     spacingX = ImGui::GetStyle().ItemSpacing.x;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacingX, 0.0f));
		const bool enter = ImGui::InputText("##name", a_state.name.data(), a_state.name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopStyleVar();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacingX, kHintGap * Theme::Scale()));

		const bool valid = !Trimmed(a_state.name.data()).empty();
		if (!valid) {
			ImGui::TextDisabled("Enter a name.");
		} else if (!a_state.error.empty()) {
			DrawError(a_state.error);
		} else {
			ImGui::NewLine();
		}
		ImGui::PopStyleVar();
		return enter && valid;
	}

	// Tailor's gender values as one row of radio buttons. API 1.0 cannot change
	// an outfit's gender after CreateOutfit; only the player can, in Tailor.
	void DrawGenderField(State& a_state)
	{
		struct GenderOption
		{
			const char* label;
			int         value;  // Tailor's value
		};
		constexpr std::array<GenderOption, 3> kGenderOptions{ {
			{ "Unisex", -1 },
			{ "Male", 0 },
			{ "Female", 1 },
		} };

		Theme::LabelText("Gender");
		for (std::size_t i = 0; i < kGenderOptions.size(); ++i) {
			if (i > 0) {
				ImGui::SameLine();
			}
			ImGui::RadioButton(kGenderOptions[i].label, &a_state.gender, kGenderOptions[i].value);
		}
	}

	enum class NameAction
	{
		kNone,
		kSave,
		kBack
	};

	// Create / Back centred below a gap.
	// a_save is true when the name field already asked to save.
	NameAction DrawNameButtons(const State& a_state, bool a_save)
	{
		BeginButtonRow({ "Create", "Back" });
		const bool valid = !Trimmed(a_state.name.data()).empty();
		ImGui::BeginDisabled(!valid);
		const bool save = Theme::Button("Create") || a_save;
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Saving...");
			return NameAction::kNone;
		}
		if (save && valid) {
			return NameAction::kSave;
		}
		return back ? NameAction::kBack : NameAction::kNone;
	}

	void RequestSave(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		const auto               name = Trimmed(a_state.name.data());
		const auto               pieces = TickedPieces(a_state);
		std::vector<std::string> categories;
		for (std::size_t i = 0; i < a_state.request.categories.size(); ++i) {
			if (a_state.ticked[i]) {
				categories.push_back(a_state.request.categories[i].name);
			}
		}
		auto       onSave = a_state.request.onSave;
		const auto gender = a_state.gender;
		SKSE::GetTaskInterface()->AddTask([onSave, name, pieces, categories, gender] { onSave(name, pieces, categories, gender); });
	}

	// Reached only with at least one piece ticked, which onSave relies on;
	// the ticks cannot change here.
	void DrawNamePage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		const bool save = DrawNameField(a_state);
		DrawCategoryField(a_state, "##categories", a_state.ticked.get(), nullptr);
		DrawGenderField(a_state);
		const auto action = DrawNameButtons(a_state, save);
		ImGui::EndDisabled();

		switch (action) {
		case NameAction::kSave:
			RequestSave(a_state);
			break;
		case NameAction::kBack:
			GoToPage(a_state, Page::kPieces);
			break;
		case NameAction::kNone:
			break;
		}
	}

	// --- Update page ----------------------------------------------------------

	// Starts the category ticks from the outfit's own categories, which are
	// locked: API 1.0 can add an outfit to a category but not take it out.
	void SelectOutfit(State& a_state, int a_index)
	{
		if (a_state.selectedOutfit == a_index) {
			return;  // keep the user's ticks
		}
		a_state.selectedOutfit = a_index;
		const auto& joined = a_state.request.existingOutfits[static_cast<std::size_t>(a_index)].categories;
		const auto& options = a_state.request.categories;
		for (std::size_t i = 0; i < options.size(); ++i) {
			const bool in = std::ranges::find(joined, options[i].name) != joined.end();
			a_state.updateLocked[i] = in;
			a_state.updateTicked[i] = in;
		}
	}

	// The categories the user ticked that the selected outfit is not in yet.
	std::vector<std::string> AddedCategories(const State& a_state)
	{
		std::vector<std::string> added;
		for (std::size_t i = 0; i < a_state.request.categories.size(); ++i) {
			if (a_state.updateTicked[i] && !a_state.updateLocked[i]) {
				added.push_back(a_state.request.categories[i].name);
			}
		}
		return added;
	}

	// Left column: the library's outfits; clicking one selects it. Names are
	// ImGui labels, so one containing "##" shows only up to it.
	void DrawOutfitList(State& a_state)
	{
		const auto& outfits = a_state.request.existingOutfits;
		Theme::LabelText(std::format("{} outfit(s) in Tailor", outfits.size()).c_str());
		const float listHeight = ListHeight(outfits.size(), ImGui::GetTextLineHeightWithSpacing());
		if (ImGui::BeginChild("outfits", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
			for (std::size_t i = 0; i < outfits.size(); ++i) {
				const int index = static_cast<int>(i);
				ImGui::PushID(index);
				if (ImGui::Selectable(outfits[i].name.c_str(), a_state.selectedOutfit == index)) {
					SelectOutfit(a_state, index);
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}

	// Right column: the selected outfit as Tailor had it when the window opened,
	// with a dropdown to add it to more categories.
	void DrawOutfitDetails(State& a_state)
	{
		if (a_state.selectedOutfit < 0) {
			Theme::LabelText("Categories");
			ImGui::TextDisabled("Select an outfit.");
			return;
		}
		const auto& outfit = a_state.request.existingOutfits[static_cast<std::size_t>(a_state.selectedOutfit)];
		DrawCategoryField(a_state, "##updateCategories", a_state.updateTicked.get(), a_state.updateLocked.get());

		Theme::LabelText(std::format("{} piece(s)", outfit.pieces.size()).c_str());
		const float listHeight = ListHeight(outfit.pieces.size(), ImGui::GetTextLineHeightWithSpacing());
		if (ImGui::BeginChild("outfitPieces", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
			for (const auto& piece : outfit.pieces) {
				ImGui::TextUnformatted(piece.c_str());
			}
		}
		ImGui::EndChild();
	}

	// Update / Back centred below a gap; Update only opens the confirm popup.
	// Returns true when the user asked to go back to the Pieces page.
	// Contract: called in the window's own ID scope, as DrawConfirmPopup is, so
	// OpenPopup and BeginPopupModal name the same popup.
	bool DrawUpdateButtons(State& a_state)
	{
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		BeginButtonRow({ "Update", "Back" });
		ImGui::BeginDisabled(a_state.selectedOutfit < 0);
		if (Theme::Button("Update")) {
			ImGui::OpenPopup(kConfirmPopup);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		// While the popup is open, Escape is its own Cancel.
		const bool back = Theme::Button("Back") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen(kConfirmPopup));

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Saving...");
			return false;
		}
		return back;
	}

	// Returns true when the user confirmed replacing the selected outfit's pieces
	// and adding it to the newly ticked categories.
	// Contract: called every frame on the Update page: a modal that is open but
	// not submitted stays invisible and still blocks the window.
	bool DrawConfirmPopup(const State& a_state)
	{
		const auto& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		auto flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
		if (!Theme::HasTitleBar()) {
			flags |= ImGuiWindowFlags_NoTitleBar;
		}
		if (!ImGui::BeginPopupModal(kConfirmPopup, nullptr, flags)) {
			return false;
		}
		if (a_state.selectedOutfit < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		Theme::DecorateWindow();

		const auto& outfit = a_state.request.existingOutfits[static_cast<std::size_t>(a_state.selectedOutfit)];
		ImGui::TextUnformatted(std::format("Replace the {} piece(s) of \"{}\"", outfit.pieces.size(), outfit.name).c_str());
		ImGui::TextUnformatted(std::format("with the {} ticked piece(s)?", CountTickedPieces(a_state)).c_str());
		if (const auto added = AddedCategories(a_state); added.empty()) {
			ImGui::TextDisabled("Its categories stay as they are.");
		} else {
			ImGui::TextUnformatted(std::format("Also add it to: {}.", JoinNames(added)).c_str());
			ImGui::TextDisabled("Categories cannot be removed later.");
		}
		BeginButtonRow({ "Confirm", "Cancel" });
		const bool confirm = Theme::Button("Confirm");
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		if (confirm || cancel) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return confirm;
	}

	void RequestUpdate(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		const auto name = a_state.request.existingOutfits[static_cast<std::size_t>(a_state.selectedOutfit)].name;
		const auto pieces = TickedPieces(a_state);
		const auto categories = AddedCategories(a_state);
		auto       onUpdate = a_state.request.onUpdate;
		SKSE::GetTaskInterface()->AddTask([onUpdate, name, pieces, categories] { onUpdate(name, pieces, categories); });
	}

	void DrawUpdatePage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		if (BeginColumns()) {
			ImGui::TableNextColumn();
			DrawOutfitList(a_state);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			DrawOutfitDetails(a_state);
			EndColumns();
		}
		const bool back = DrawUpdateButtons(a_state);
		ImGui::EndDisabled();

		if (DrawConfirmPopup(a_state)) {
			RequestUpdate(a_state);
		} else if (back) {
			GoToPage(a_state, Page::kPieces);
		}
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
			state.updateTicked = std::make_unique<bool[]>(state.request.categories.size());  // filled by SelectOutfit
			state.updateLocked = std::make_unique<bool[]>(state.request.categories.size());
			state.gender = state.request.suggestedGender;
			state.error.clear();
			state.pending.clear();
			state.focusNameField = false;
			state.status = Status::kEditing;
			state.resetImGui = true;
			state.page = Page::kPieces;
			state.selectedOutfit = -1;
			state.recentreFrames = 0;  // the first frame centres the window anyway (ImGuiCond_Appearing)
		}
		g_open = true;
		SetGameFrozen(true);
		Hooks::ShowMenu();
	}

	void Close()
	{
		auto&                 state = GetState();
		std::function<void()> onClose;
		{
			const std::scoped_lock lock(state.mutex);
			if (state.status == Status::kClosed) {
				return;
			}
			onClose = std::move(state.request.onClose);
			state.status = Status::kClosed;
			state.request = {};
			state.ticked.reset();
			state.updateTicked.reset();
			state.updateLocked.reset();
			state.pending.clear();
		}
		g_open = false;  // before HideMenu: the menu's hide handler tells our hide from the game's by this
		SetGameFrozen(false);
		Hooks::HideMenu();
		if (onClose) {
			onClose();  // outside the lock: it calls Tailor
		}
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

		const ImGuiCond posCondition = state.recentreFrames > 0 ? ImGuiCond_Always : ImGuiCond_Appearing;
		state.recentreFrames = (std::max)(state.recentreFrames - 1, 0);
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), posCondition, ImVec2(0.5f, 0.5f));
		constexpr float kWindowWidthRatio = 0.45f;  // of the screen width, for the two-column Update page
		constexpr float kNarrowWidthRatio = 0.30f;  // of the screen width, for the one-column pages
		const float     widthRatio = state.page == Page::kUpdate ? kWindowWidthRatio : kNarrowWidthRatio;
		// Width is fixed every frame; the height follows the content (a zero
		// axis is left to AlwaysAutoResize), so the window never needs to scroll.
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * widthRatio, 0.0f), ImGuiCond_Always);
		auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
		if (!Theme::HasTitleBar()) {
			flags |= ImGuiWindowFlags_NoTitleBar;
		}
		if (!ImGui::Begin("Capturing Outfit for Tailor", nullptr, flags)) {
			ImGui::End();
			return;
		}
		Theme::DecorateWindow();

		Theme::HeadingText(state.request.actorName.c_str(), state.request.actorSummary.c_str());
		switch (state.page) {
		case Page::kPieces:
			DrawPiecesPage(state);
			break;
		case Page::kName:
			DrawNamePage(state);
			break;
		case Page::kUpdate:
			DrawUpdatePage(state);
			break;
		}
		ImGui::End();
	}
}
