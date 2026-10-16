// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State        - one mutex-guarded struct: request, name buffer, category ticks, status
//   Game thread  - Open / Close / SetError (also freeze and unfreeze the game, unless previewing)
//   Input        - QueueInput translates engine events and moves the game's cursor;
//                  FeedInput hands them and the cursor position to ImGui
//   Draw         - page switching, shared list and button-row helpers; mouse and
//                  key input over the game goes to onViewInput
//   Pieces page  - inventory pieces (first page) plus the loaded outfit's, each tick
//                  handed to onPieceToggled; Load Outfit / Create New / Update Existing /
//                  Cancel, or Apply in place of the two saves while the ticks are
//                  exactly the loaded outfit
//   Name page    - name, categories and gender of the new outfit; Create / Back
//   Outfit lists - the library outfit list and piece list both outfit pages share
//   Load page    - library outfits, the selected one shown on the preview; Load ticks
//                  its pieces and returns, Back restores the preview of the ticks
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
#include <optional>
#include <utility>

namespace
{
	constexpr std::size_t kNameCapacity = 64;                 // bytes, UTF-8, including the terminator
	constexpr float       kMaxListHeightRatio = 0.6f;         // of the screen height, for every scrolling list
	constexpr const char* kConfirmPopup = "Replace pieces?";  // ID shared by OpenPopup and BeginPopupModal
	// Frames the window's place is pinned after Open or a page change: an
	// auto-resized window fits new content a frame late, and PlaceWindow sees
	// the fitted height a frame after that.
	constexpr int kRecentreFrames = 3;

	enum class Status
	{
		kClosed,
		kEditing,
		kSaving  // Create, Confirm or Apply pressed; waiting for onSave / onUpdate / onApply to Close() or SetError()
	};

	enum class Page
	{
		kPieces,
		kName,
		kLoad,
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
		int                             selectedOutfit = -1;   // index into request.existingOutfits, -1 for none
		int                             previewedOutfit = -1;  // on the Load page: index into request.existingOutfits, -1 for none
		int                             loadedOutfit = -1;     // the last one loaded: index into request.existingOutfits, -1 for none
		int                             recentreFrames = 0;    // frames left to pin the window to its place
		std::optional<ImVec2>           placeCentre;           // where a pinned window's centre goes; none for the start place
		ImVec2                          windowPos{};           // of the window in the last frame drawn
		ImVec2                          windowSize{};
		bool                            frozeGame = false;    // Open froze the game, so Close unfreezes it
		int                             viewDragButton = -1;  // ImGui mouse button held since a press over the game; -1 for none
		float                           viewDragX = 0.0f;     // mouse movement since the last frame while viewDragButton is held
		float                           viewDragY = 0.0f;
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
		case Key::kD:
			return ImGuiKey_D;
		case Key::kS:
			return ImGuiKey_S;
		case Key::kW:
			return ImGuiKey_W;
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

	// The new page is centred where the current one is (the user may have moved
	// it); PlaceWindow keeps it inside the screen.
	void GoToPage(State& a_state, Page a_page)
	{
		a_state.page = a_page;
		a_state.error.clear();
		a_state.focusNameField = a_page == Page::kName;
		a_state.placeCentre = ImVec2(a_state.windowPos.x + a_state.windowSize.x * 0.5f, a_state.windowPos.y + a_state.windowSize.y * 0.5f);
		a_state.recentreFrames = kRecentreFrames;
	}

	// While pinned, centres the window on placeCentre, or on its start place:
	// vertically centred, and on the left beside a preview camera's view or
	// else in the middle. The centre is kept so the page's width and the last
	// frame's height fit the screen; one taller than the screen keeps its top
	// edge on it. Unpinned, the window stays where the user left it.
	void PlaceWindow(State& a_state, const ImGuiIO& a_io, float a_width)
	{
		constexpr float kSideMarginRatio = 0.03f;  // of the screen width, between its left edge and the window beside a preview

		if (a_state.recentreFrames == 0) {
			return;
		}
		--a_state.recentreFrames;
		const auto&  screen = a_io.DisplaySize;
		const bool   aside = static_cast<bool>(a_state.request.onViewInput);
		const ImVec2 start(aside ? screen.x * kSideMarginRatio + a_width * 0.5f : screen.x * 0.5f, screen.y * 0.5f);
		const ImVec2 centre = a_state.placeCentre.value_or(start);
		const auto   inside = [](float a_centre, float a_extent, float a_limit) {
			return (std::max)(a_extent * 0.5f, (std::min)(a_centre, a_limit - a_extent * 0.5f));
		};
		const ImVec2 fitted(inside(centre.x, a_width, screen.x), inside(centre.y, a_state.windowSize.y, screen.y));
		ImGui::SetNextWindowPos(fitted, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
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

	const CaptureWindow::ExistingOutfit& OutfitAt(const State& a_state, int a_index)
	{
		return a_state.request.existingOutfits[static_cast<std::size_t>(a_index)];
	}

	bool HasPiece(const CaptureWindow::ExistingOutfit& a_outfit, std::size_t a_piece)
	{
		return std::ranges::binary_search(a_outfit.pieces, a_piece);
	}

	// Pieces the actor carries, and those of the loaded outfit.
	bool IsListed(const State& a_state, std::size_t a_piece)
	{
		return a_state.request.pieces[a_piece].owned || (a_state.loadedOutfit >= 0 && HasPiece(OutfitAt(a_state, a_state.loadedOutfit), a_piece));
	}

	std::size_t CountListedPieces(const State& a_state)
	{
		std::size_t listed = 0;
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			listed += IsListed(a_state, i) ? 1 : 0;
		}
		return listed;
	}

	void QueuePreview(const State& a_state, std::vector<std::size_t> a_pieces)
	{
		if (const auto& onPreview = a_state.request.onPreviewPieces) {
			SKSE::GetTaskInterface()->AddTask([onPreview, pieces = std::move(a_pieces)] { onPreview(pieces); });
		}
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

	// Width of a row of buttons with these labels (Theme::Button, one item
	// spacing apart).
	float ButtonRowWidth(std::initializer_list<const char*> a_labels)
	{
		float width = ImGui::GetStyle().ItemSpacing.x * static_cast<float>(a_labels.size() - 1);
		for (const auto* label : a_labels) {
			width += Theme::ButtonWidth(label);
		}
		return width;
	}

	// A gap, then the cursor placed so a row of buttons with these labels is centred.
	void BeginButtonRow(std::initializer_list<const char*> a_labels)
	{
		constexpr float kGap = 17.0f;  // pixels at 1080p, on top of one extra item spacing

		const auto& style = ImGui::GetStyle();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.ItemSpacing.y + kGap * Theme::Scale());
		const float width = ButtonRowWidth(a_labels);
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

	// Input over the game goes to onViewInput, in one SKSE task per frame that
	// has any. A drag is a button held since a press the window did not own; the
	// mouse moves of the frame it was held (collected by QueueInput) are a look
	// for the left button and a pan for the right. Keys count while no item is
	// active and no popup is open (WantCaptureKeyboard), the wheel while the
	// cursor is over the game.
	void ForwardViewInput(State& a_state)
	{
		const auto& onViewInput = a_state.request.onViewInput;
		if (!onViewInput) {
			return;
		}
		const auto&              io = ImGui::GetIO();
		CaptureWindow::ViewInput input;
		if (a_state.viewDragButton == ImGuiMouseButton_Left) {
			input.lookX = a_state.viewDragX;
			input.lookY = a_state.viewDragY;
		} else if (a_state.viewDragButton == ImGuiMouseButton_Right) {
			input.panX = a_state.viewDragX;
			input.panY = a_state.viewDragY;
		}
		a_state.viewDragX = 0.0f;
		a_state.viewDragY = 0.0f;
		a_state.viewDragButton = -1;
		for (const int button : { ImGuiMouseButton_Left, ImGuiMouseButton_Right }) {
			if (ImGui::IsMouseDown(button) && !io.MouseDownOwned[button]) {
				a_state.viewDragButton = button;
				break;
			}
		}
		if (!io.WantCaptureMouse) {
			input.zoomSteps = io.MouseWheel;
		}
		if (!io.WantCaptureKeyboard) {
			input.moveForward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
			input.moveRight = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
			input.slow = io.KeyShift;
		}
		input.seconds = io.DeltaTime;
		const bool any = input.lookX != 0.0f || input.lookY != 0.0f || input.panX != 0.0f || input.panY != 0.0f ||
		                 input.moveForward != 0.0f || input.moveRight != 0.0f || input.zoomSteps != 0.0f;
		if (any) {
			SKSE::GetTaskInterface()->AddTask([onViewInput, input] { onViewInput(input); });
		}
	}

	// --- Pieces page ----------------------------------------------------------

	void SelectOutfit(State& a_state, int a_index);  // Update page

	// Ticking a piece unticks the ticked ones sharing a body slot with it, as
	// equipping it takes them off; their onPieceToggled tasks go first.
	void UntickSharingSlots(State& a_state, std::size_t a_index)
	{
		const auto& pieces = a_state.request.pieces;
		const auto& onToggled = a_state.request.onPieceToggled;
		for (std::size_t j = 0; j < pieces.size(); ++j) {
			if (j == a_index || !a_state.pieceTicked[j] || (pieces[j].slots & pieces[a_index].slots) == 0) {
				continue;
			}
			a_state.pieceTicked[j] = false;
			if (onToggled) {
				SKSE::GetTaskInterface()->AddTask([onToggled, j] { onToggled(j, false); });
			}
		}
	}

	// A checkbox per listed piece; unticked pieces are left out of the outfit.
	// Each click goes to onPieceToggled (the live preview) in its own SKSE task,
	// queued in click order.
	// Contract: a piece that is not listed is never ticked: only Load ticks a
	// piece the actor does not carry, and the next Load sets every tick again.
	void DrawPieceList(State& a_state)
	{
		const auto& pieces = a_state.request.pieces;
		const auto& onToggled = a_state.request.onPieceToggled;
		const auto  listed = CountListedPieces(a_state);
		Theme::LabelText(std::format("{} of {} piece(s) selected", CountTickedPieces(a_state), listed).c_str());
		const float listHeight = ListHeight(listed, ImGui::GetFrameHeightWithSpacing());
		if (ImGui::BeginChild("pieces", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
			for (std::size_t i = 0; i < pieces.size(); ++i) {
				if (!IsListed(a_state, i)) {
					continue;
				}
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Checkbox(pieces[i].name.c_str(), &a_state.pieceTicked[i])) {
					if (a_state.pieceTicked[i]) {
						UntickSharingSlots(a_state, i);
					}
					if (onToggled) {
						SKSE::GetTaskInterface()->AddTask([onToggled, i, ticked = a_state.pieceTicked[i]] { onToggled(i, ticked); });
					}
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

	// The widest button row of the Pieces page, which sets the page's least width.
	float PiecesButtonsWidth()
	{
		return ButtonRowWidth({ "Load Outfit", "Create New", "Update Existing", "Cancel" });
	}

	// Apply is offered while onApply is set (a live preview) and the ticks are
	// exactly the loaded outfit's pieces: nothing added, nothing taken out.
	bool CanApply(const State& a_state)
	{
		if (!a_state.request.onApply || a_state.loadedOutfit < 0) {
			return false;
		}
		const auto& outfit = OutfitAt(a_state, a_state.loadedOutfit);
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			if (a_state.pieceTicked[i] != HasPiece(outfit, i)) {
				return false;
			}
		}
		return !outfit.pieces.empty();
	}

	enum class PiecesAction
	{
		kNone,
		kLoadPage,
		kNamePage,
		kUpdatePage,
		kApply,
		kCancel
	};

	// Load Outfit / Create New / Update Existing / Cancel centred below a gap,
	// or Load Outfit / Apply / Cancel while CanApply. The two save pages act on
	// the ticked pieces, so both need at least one.
	PiecesAction DrawPiecesButtons(const State& a_state)
	{
		const bool apply = CanApply(a_state);
		if (apply) {
			BeginButtonRow({ "Load Outfit", "Apply", "Cancel" });
		} else {
			BeginButtonRow({ "Load Outfit", "Create New", "Update Existing", "Cancel" });
		}
		const bool hasOutfits = !a_state.request.existingOutfits.empty();
		ImGui::BeginDisabled(!hasOutfits);
		const bool load = Theme::Button("Load Outfit");
		ImGui::EndDisabled();
		ImGui::SameLine();

		const bool hasPieces = CountTickedPieces(a_state) > 0;
		bool       create = false;
		bool       update = false;
		bool       applied = false;
		if (apply) {
			applied = Theme::Button("Apply");
		} else {
			ImGui::BeginDisabled(!hasPieces);
			create = Theme::Button("Create New");
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!hasPieces || !hasOutfits);
			update = Theme::Button("Update Existing");
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Applying...");
			return PiecesAction::kNone;
		}
		if (load && hasOutfits) {
			return PiecesAction::kLoadPage;
		}
		if (applied) {
			return PiecesAction::kApply;
		}
		if (create && hasPieces) {
			return PiecesAction::kNamePage;
		}
		if (update && hasPieces && hasOutfits) {
			return PiecesAction::kUpdatePage;
		}
		return cancel ? PiecesAction::kCancel : PiecesAction::kNone;
	}

	void RequestApply(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onApply = a_state.request.onApply;
		auto name = OutfitAt(a_state, a_state.loadedOutfit).name;
		SKSE::GetTaskInterface()->AddTask([onApply, name = std::move(name)] { onApply(name); });
	}

	void DrawPiecesPage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		DrawPieceList(a_state);
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		const auto action = DrawPiecesButtons(a_state);
		ImGui::EndDisabled();

		switch (action) {
		case PiecesAction::kLoadPage:
			a_state.previewedOutfit = -1;
			GoToPage(a_state, Page::kLoad);
			break;
		case PiecesAction::kNamePage:
			GoToPage(a_state, Page::kName);
			break;
		case PiecesAction::kUpdatePage:
			if (a_state.loadedOutfit >= 0) {
				SelectOutfit(a_state, a_state.loadedOutfit);
			}
			GoToPage(a_state, Page::kUpdate);
			break;
		case PiecesAction::kApply:
			RequestApply(a_state);
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

	// --- Outfit lists ---------------------------------------------------------

	// Left column of the Load and Update pages: the library's outfits, `a_selected`
	// highlighted. Returns the index of the one clicked, or -1. Names are ImGui
	// labels, so one containing "##" shows only up to it.
	int DrawOutfitList(const State& a_state, int a_selected)
	{
		const auto& outfits = a_state.request.existingOutfits;
		Theme::LabelText(std::format("{} outfit(s) in Tailor", outfits.size()).c_str());
		const float listHeight = ListHeight(outfits.size(), ImGui::GetTextLineHeightWithSpacing());
		int         clicked = -1;
		if (ImGui::BeginChild("outfits", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
			for (std::size_t i = 0; i < outfits.size(); ++i) {
				const int index = static_cast<int>(i);
				ImGui::PushID(index);
				if (ImGui::Selectable(outfits[i].name.c_str(), a_selected == index)) {
					clicked = index;
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		return clicked;
	}

	// The outfit's pieces as Tailor had them when the window opened.
	void DrawOutfitPieces(const State& a_state, const CaptureWindow::ExistingOutfit& a_outfit)
	{
		Theme::LabelText(std::format("{} piece(s)", a_outfit.pieces.size()).c_str());
		const float listHeight = ListHeight(a_outfit.pieces.size(), ImGui::GetTextLineHeightWithSpacing());
		if (ImGui::BeginChild("outfitPieces", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
			for (const auto index : a_outfit.pieces) {
				ImGui::TextUnformatted(a_state.request.pieces[index].name.c_str());
			}
		}
		ImGui::EndChild();
	}

	// --- Load page ------------------------------------------------------------

	// Right column: the previewed outfit's categories and pieces, read only.
	void DrawLoadDetails(const State& a_state)
	{
		Theme::LabelText("Categories");
		if (a_state.previewedOutfit < 0) {
			ImGui::TextDisabled("Select an outfit.");
			return;
		}
		const auto& outfit = OutfitAt(a_state, a_state.previewedOutfit);
		const auto  categories = outfit.categories.empty() ? std::string("None") : JoinNames(outfit.categories);
		ImGui::TextWrapped("%s", categories.c_str());
		DrawOutfitPieces(a_state, outfit);
	}

	enum class LoadAction
	{
		kNone,
		kLoad,
		kBack
	};

	// Load / Back centred below a gap.
	LoadAction DrawLoadButtons(const State& a_state)
	{
		BeginButtonRow({ "Load", "Back" });
		const bool canLoad = a_state.previewedOutfit >= 0;
		ImGui::BeginDisabled(!canLoad);
		const bool load = Theme::Button("Load");
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

		if (load && canLoad) {
			return LoadAction::kLoad;
		}
		return back ? LoadAction::kBack : LoadAction::kNone;
	}

	// Ticks exactly the previewed outfit's pieces, which the preview already
	// shows; those the actor does not carry stay listed while it is loaded.
	void LoadOutfit(State& a_state)
	{
		const auto& outfit = OutfitAt(a_state, a_state.previewedOutfit);
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			a_state.pieceTicked[i] = HasPiece(outfit, i);
		}
		a_state.loadedOutfit = a_state.previewedOutfit;
	}

	// Selecting an outfit shows it on the preview; Back shows the ticks again.
	// Nothing here changes the ticks until Load.
	void DrawLoadPage(State& a_state)
	{
		int clicked = -1;
		if (BeginColumns()) {
			ImGui::TableNextColumn();
			clicked = DrawOutfitList(a_state, a_state.previewedOutfit);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			DrawLoadDetails(a_state);
			EndColumns();
		}
		if (clicked >= 0 && clicked != a_state.previewedOutfit) {
			a_state.previewedOutfit = clicked;
			QueuePreview(a_state, OutfitAt(a_state, clicked).pieces);
		}

		switch (DrawLoadButtons(a_state)) {
		case LoadAction::kLoad:
			LoadOutfit(a_state);
			GoToPage(a_state, Page::kPieces);
			break;
		case LoadAction::kBack:
			if (a_state.previewedOutfit >= 0) {
				QueuePreview(a_state, TickedPieces(a_state));
			}
			GoToPage(a_state, Page::kPieces);
			break;
		case LoadAction::kNone:
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

	// Right column: the selected outfit as Tailor had it when the window opened,
	// with a dropdown to add it to more categories.
	void DrawOutfitDetails(State& a_state)
	{
		if (a_state.selectedOutfit < 0) {
			Theme::LabelText("Categories");
			ImGui::TextDisabled("Select an outfit.");
			return;
		}
		DrawCategoryField(a_state, "##updateCategories", a_state.updateTicked.get(), a_state.updateLocked.get());
		DrawOutfitPieces(a_state, OutfitAt(a_state, a_state.selectedOutfit));
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
			if (const int clicked = DrawOutfitList(a_state, a_state.selectedOutfit); clicked >= 0) {
				SelectOutfit(a_state, clicked);
			}
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
			state.previewedOutfit = -1;
			state.loadedOutfit = -1;
			state.recentreFrames = kRecentreFrames;
			state.placeCentre.reset();  // the start place
			state.viewDragButton = -1;
			state.viewDragX = 0.0f;
			state.viewDragY = 0.0f;
			// A live preview needs the game running: the engine applies an NPC's
			// queued equips in the actor update, which a frozen game skips.
			state.frozeGame = !state.request.onPieceToggled;
		}
		g_open = true;
		if (state.frozeGame) {
			SetGameFrozen(true);
		}
		Hooks::ShowMenu();
	}

	void Close()
	{
		auto&                 state = GetState();
		std::function<void()> onClose;
		bool                  frozeGame = false;
		{
			const std::scoped_lock lock(state.mutex);
			if (state.status == Status::kClosed) {
				return;
			}
			onClose = std::move(state.request.onClose);
			frozeGame = std::exchange(state.frozeGame, false);
			state.status = Status::kClosed;
			state.request = {};
			state.ticked.reset();
			state.updateTicked.reset();
			state.updateLocked.reset();
			state.pending.clear();
		}
		g_open = false;  // before HideMenu: the menu's hide handler tells our hide from the game's by this
		if (frozeGame) {
			SetGameFrozen(false);
		}
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

	// Contract: the game sees none of these events (Hooks.cpp), so the Cursor
	// Menu is handed the mouse moves here, as Photo Mode does; during a drag over
	// the game they turn the camera instead and the cursor stays put.
	void QueueInput(RE::InputEvent* const* a_events)
	{
		auto&                    state = GetState();
		const std::scoped_lock   lock(state.mutex);
		RE::GPtr<RE::CursorMenu> cursorMenu;
		for (auto* event = *a_events; event; event = event->next) {
			if (const auto* button = event->AsButtonEvent()) {
				RecordButton(state.pending, *button);
			} else if (const auto* chr = event->AsCharEvent()) {
				// ASCII only: the game's char events carry the ANSI code page.
				if (chr->keyCode >= 0x20 && chr->keyCode < 0x7F) {
					state.pending.push_back({ InputRecord::Kind::kChar, static_cast<int>(chr->keyCode), true, 0.0f });
				}
			} else if (auto* move = event->AsMouseMoveEvent()) {
				if (state.viewDragButton >= 0) {
					state.viewDragX += static_cast<float>(move->mouseInputX);
					state.viewDragY += static_cast<float>(move->mouseInputY);
					continue;
				}
				if (!cursorMenu) {
					if (auto* ui = RE::UI::GetSingleton()) {
						cursorMenu = ui->GetMenu<RE::CursorMenu>();
					}
				}
				if (cursorMenu) {
					cursorMenu->AsMenuEventHandler()->ProcessMouseMove(move);  // via the accessor: the SE/AE/VR build has no static base
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

		constexpr float kWindowWidthRatio = 0.45f;  // of the screen width, for the two-column Load and Update pages
		constexpr float kNarrowWidthRatio = 0.30f;  // of the screen width, for the one-column pages
		const bool      twoColumns = state.page == Page::kLoad || state.page == Page::kUpdate;
		float           width = io.DisplaySize.x * (twoColumns ? kWindowWidthRatio : kNarrowWidthRatio);
		if (state.page == Page::kPieces) {
			width = (std::max)(width, PiecesButtonsWidth() + ImGui::GetStyle().WindowPadding.x * 2.0f);  // a large font's buttons still fit
		}
		PlaceWindow(state, io, width);
		// Width is fixed every frame; the height follows the content (a zero
		// axis is left to AlwaysAutoResize), so the window never needs to scroll.
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
		if (!Theme::HasTitleBar()) {
			flags |= ImGuiWindowFlags_NoTitleBar;
		}
		if (!ImGui::Begin("Capturing Outfit for Tailor", nullptr, flags)) {
			ImGui::End();
			return;
		}
		state.windowPos = ImGui::GetWindowPos();
		state.windowSize = ImGui::GetWindowSize();
		Theme::DecorateWindow();

		Theme::HeadingText(state.request.actorName.c_str(), state.request.actorSummary.c_str());
		switch (state.page) {
		case Page::kPieces:
			DrawPiecesPage(state);
			break;
		case Page::kName:
			DrawNamePage(state);
			break;
		case Page::kLoad:
			DrawLoadPage(state);
			break;
		case Page::kUpdate:
			DrawUpdatePage(state);
			break;
		}
		ImGui::End();
		ForwardViewInput(state);
	}
}
