// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State        - one mutex-guarded struct: request, name buffer, category ticks, status
//   Game thread  - Open / Close / SetError (also freeze and unfreeze the game while Live Preview is off)
//   Input        - QueueInput translates engine events and moves the game's cursor;
//                  FeedInput hands them and the cursor position to ImGui
//   Draw         - page switching, shared list and button-row helpers; mouse and
//                  key input over the game goes to onViewInput
//   Capture page - the actor's pieces under a heading per group (their Livery outfit, other
//                  worn, carried, the player's, the loaded outfit's), in Name / Type / Class
//                  columns sorted by a click on a column label, each tick handed to
//                  onPieceToggled; under the list the count line with Load Outfit... at its
//                  right end (greyed while the library has no outfits); the Live Preview tick,
//                  handed to onPreviewOn / onPreviewOff in an SKSE task that also freezes or
//                  unfreezes the game; Save / Update Existing / Apply / Cancel, and Restore
//                  Original Outfit under them while Livery dresses the actor; Apply and
//                  Restore hand over to onApply / onRestore
//   Name page    - name, categories and gender of the new outfit; Create / Back
//   Outfit browser - Search over Filter, above both outfit pages' list in its column, in
//                  the small font (OutfitBrowser.cpp narrows and orders it); Escape first
//                  clears the search
//   Outfit lists - the library outfit list with its Sort dropdown, the selected
//                  outfit's name (or a hint while none is) and its piece list, which
//                  both outfit pages share;
//                  on both, the selected outfit is on the preview, and a second
//                  click on it, or Back, shows the ticks again
//   Load page    - library outfits and the selected one's details; Load ticks its
//                  pieces and returns (greyed while none of them is loaded)
//   Update page  - library outfits, the chosen one's categories (ticked as it
//                  has them, free to change) and pieces; Update opens the
//                  confirm popup, which names the pieces from unloaded plugins
//                  that stay, and whose Confirm hands over to onUpdate
//   Menu sounds  - the game's menu sounds on Open and on clicks, unless turned off in Settings
#include "PCH.h"

#include "CaptureWindow.h"

#include "Hooks.h"
#include "OutfitBrowser.h"
#include "Settings.h"
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
	// Reasons a button is greyed out, shown on hover (and in the empty name field).
	constexpr const char* kNeedName = "Enter a name.";
	constexpr const char* kNeedPiece = "Tick at least one piece.";
	constexpr const char* kNeedOutfit = "Select an outfit.";
	constexpr const char* kNoOutfits = "No outfits in the library.";
	constexpr const char* kNoLoadedPiece = "None of its pieces is loaded.";
	// Frames the window's place is pinned after Open or a page change: an
	// auto-resized window fits new content a frame late, and PlaceWindow sees
	// the fitted height a frame after that.
	constexpr int kRecentreFrames = 3;
	// One heading per CaptureWindow::PieceGroup.
	constexpr std::size_t kPieceGroupCount = static_cast<std::size_t>(CaptureWindow::PieceGroup::kNotCarried) + 1;
	using GroupHeadings = std::array<std::string, kPieceGroupCount>;

	// The outfit lists' Sort dropdown, in its order.
	struct SortOption
	{
		const char*            label;
		OutfitBrowser::SortKey key;
	};
	constexpr std::array<SortOption, 3> kSortOptions{ {
		{ "Name", OutfitBrowser::SortKey::kName },
		{ "Class", OutfitBrowser::SortKey::kClass },
		{ "Situation", OutfitBrowser::SortKey::kSituation },
	} };

	// The piece list's columns, in their order (State::sortColumn).
	constexpr int                        kNameColumn = 0;
	constexpr int                        kTypeColumn = 1;
	constexpr int                        kClassColumn = 2;
	constexpr std::array<const char*, 3> kColumnLabels{ "Name", "Type", "Class" };

	// Sound descriptors in Skyrim.esm, whose load index is always 00.
	constexpr RE::FormID kOpenSound = 0x0003F2BD;    // UIMenuBladeOpenSD
	constexpr RE::FormID kOkSound = 0x0003C751;      // UIMenuOKSD
	constexpr RE::FormID kCancelSound = 0x0003C752;  // UIMenuCancelSD
	constexpr RE::FormID kFocusSound = 0x0003C759;   // UIMenuFocus

	enum class Status
	{
		kClosed,
		kEditing,
		kSaving  // Create, Confirm, Apply or Restore pressed; waiting for the handler to Close() or SetError()
	};

	enum class Page
	{
		kCapture,
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
		std::unique_ptr<bool[]>         pieceTicked;   // one per request.pieces; bool[] because Theme::TickRow needs bool*
		std::unique_ptr<bool[]>         ticked;        // one per request.categories
		std::unique_ptr<bool[]>         updateTicked;  // one per request.categories, for the selected outfit
		int                             gender = -1;   // of the new outfit: -1 unisex, 0 male, 1 female
		std::string                     error;
		bool                            focusNameField = false;
		std::vector<InputRecord>        pending;
		bool                            resetImGui = false;  // set by Open, handled on the next frame
		Page                            page = Page::kCapture;
		std::string                     captureTitle;    // the Capture page's title line
		GroupHeadings                   headings;        // over each group's pieces (CaptureWindow::PieceGroup)
		std::vector<std::size_t>        pieceOrder;      // indices into request.pieces as the list shows them (SortPieces)
		int                             sortColumn = 0;  // the piece list's sorted column: Name, Type or Class
		bool                            sortDescending = false;
		int                             selectedOutfit = -1;   // index into request.existingOutfits, -1 for none
		int                             previewedOutfit = -1;  // on the Load page: index into request.existingOutfits, -1 for none
		int                             loadedOutfit = -1;     // the last one loaded: index into request.existingOutfits, -1 for none
		OutfitBrowser::State            browser;               // search, filter and sort of the Load and Update pages' outfit list
		int                             recentreFrames = 0;    // frames left to pin the window to its place
		std::optional<ImVec2>           placeCentre;           // where a pinned window's centre goes; none for the start place
		ImVec2                          windowPos{};           // of the window in the last frame drawn
		ImVec2                          windowSize{};
		bool                            userMoved = false;    // the user moved the window since Open, so turning Live Preview on keeps it there
		unsigned                        session = 0;          // counts Opens, so a Live Preview task queued before a close does nothing
		bool                            previewOn = false;    // Live Preview is on
		bool                            previewBusy = false;  // a Live Preview tick waits for its SKSE task
		bool                            frozeGame = false;    // the window froze the game (Live Preview off), so Close unfreezes it
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

	// Contract: safe from any thread; the sound plays in an SKSE task on the game thread.
	void QueueMenuSound(RE::FormID a_sound)
	{
		if (!Settings::MenuSounds()) {
			return;
		}
		SKSE::GetTaskInterface()->AddTask([a_sound] {
			if (auto* audio = RE::BSAudioManager::GetSingleton()) {
				audio->Play(a_sound);
			}
		});
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
	// vertically centred, and beside the live preview with its right edge
	// kPreviewGapRatio left of the screen's middle, or else in the middle.
	// The gap follows the screen height, as the NPC in front does (the view
	// widens sideways on a wider screen, so ultrawide keeps the same gap). The centre is kept so the page's width and the last frame's
	// height fit the screen; one taller than the screen keeps its top edge on
	// it. Unpinned, the window stays where the user left it. Returns whether
	// it was pinned this frame.
	bool PlaceWindow(State& a_state, const ImGuiIO& a_io, float a_width)
	{
		constexpr float kPreviewGapRatio = 0.20f;  // of the screen height, from the screen's middle to the right edge of the window beside a preview

		if (a_state.recentreFrames == 0) {
			return false;
		}
		--a_state.recentreFrames;
		const auto&  screen = a_io.DisplaySize;
		const bool   aside = a_state.previewOn;
		const ImVec2 start(aside ? screen.x * 0.5f - screen.y * kPreviewGapRatio - a_width * 0.5f : screen.x * 0.5f, screen.y * 0.5f);
		const ImVec2 centre = a_state.placeCentre.value_or(start);
		const auto   inside = [](float a_centre, float a_extent, float a_limit) {
			return (std::max)(a_extent * 0.5f, (std::min)(a_centre, a_limit - a_extent * 0.5f));
		};
		const ImVec2 fitted(inside(centre.x, a_width, screen.x), inside(centre.y, a_state.windowSize.y, screen.y));
		ImGui::SetNextWindowPos(fitted, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		return true;
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

	// Pieces someone carries, and those of the loaded outfit.
	bool IsListed(const State& a_state, std::size_t a_piece)
	{
		return a_state.request.pieces[a_piece].group != CaptureWindow::PieceGroup::kNotCarried ||
		       (a_state.loadedOutfit >= 0 && HasPiece(OutfitAt(a_state, a_state.loadedOutfit), a_piece));
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

	// The outfit pages keep their selected outfit (index into
	// request.existingOutfits) on the preview, and the ticks while none is selected.
	void PreviewOutfitOrTicks(const State& a_state, int a_outfit)
	{
		QueuePreview(a_state, a_outfit >= 0 ? OutfitAt(a_state, a_outfit).pieces : TickedPieces(a_state));
	}

	// Height of a Theme::BeginList list exactly as tall as its rows, which take
	// `a_rowsHeight` pixels with the spacing under each, so it does not scroll,
	// capped so a long list cannot push the window off screen; the cap also covers
	// `a_reserved` pixels of other content stacked above it. It is at least
	// `a_minHeight`: a child of height zero would fill the window.
	// Contract: called with the item spacing the rows will be laid out with.
	float FittedListHeight(float a_rowsHeight, float a_minHeight, float a_reserved = 0.0f)
	{
		const float fullHeight = a_rowsHeight - ImGui::GetStyle().ItemSpacing.y + Theme::ListPaddingY() * 2.0f;
		const float cap = ImGui::GetIO().DisplaySize.y * kMaxListHeightRatio - a_reserved;
		return (std::max)(a_minHeight, (std::min)(fullHeight, cap));
	}

	// FittedListHeight for rows of one height; an empty list keeps one row.
	float ListHeight(std::size_t a_rows, float a_rowHeight, float a_reserved = 0.0f)
	{
		return FittedListHeight(static_cast<float>((std::max)(a_rows, std::size_t{ 1 })) * a_rowHeight, a_rowHeight, a_reserved);
	}

	// Item spacing for a list's rows: the skin's row gap below each one.
	void PushListRowSpacing()
	{
		const auto& spacing = ImGui::GetStyle().ItemSpacing;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing.x, Theme::ListRowGap(spacing.y)));
	}

	void DrawError(const std::string& a_message)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ErrorColor());
		ImGui::TextWrapped("%s", a_message.c_str());
		ImGui::PopStyleColor();
	}

	// A button greyed out while `a_reason` is set, which shows on hover; a plain
	// Theme::Button otherwise. Returns true when clicked while enabled.
	bool ReasonButton(const char* a_label, const char* a_reason)
	{
		ImGui::BeginDisabled(a_reason != nullptr);
		const bool clicked = Theme::Button(a_label);
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		// After EndDisabled, so the tooltip is not drawn faded.
		if (a_reason && hovered) {
			ImGui::SetTooltip("%s", a_reason);
		}
		return clicked && !a_reason;
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

	// Room between the last button row (and any status line under it) and the
	// window's bottom edge, on top of the window padding. The item spacing
	// below the last item already counts toward it.
	void EndButtonRow()
	{
		constexpr float kBottomGap = 12.0f;  // pixels at 1080p

		ImGui::Dummy(ImVec2(0.0f, (std::max)(0.0f, kBottomGap * Theme::Scale() - ImGui::GetStyle().ItemSpacing.y)));
	}

	// A table hands the next cell the text baseline of the previous cell's last
	// line (a frame's padding), which would push the cell's first label down.
	void ResetCellBaseline()
	{
		ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.0f;
	}

	// The Load and Update pages' two equal columns; every table of them on a
	// page lines up, each with its own `a_id`. When it returns true, fill the
	// cells and call EndColumns.
	bool BeginColumns(const char* a_id)
	{
		constexpr float kColumnGap = 36.0f;  // pixels at 1080p, between the columns

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(kColumnGap * 0.5f * Theme::Scale(), ImGui::GetStyle().CellPadding.y));
		// NoClip lets the lists reach the window's edges (Theme::BeginList).
		if (!ImGui::BeginTable(a_id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoClip)) {
			ImGui::PopStyleVar();
			return false;
		}
		ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch);
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
	// cursor is over the game. None while Live Preview is off: a drag is
	// dropped, so QueueInput moves the cursor again.
	void ForwardViewInput(State& a_state)
	{
		const auto& onViewInput = a_state.request.onViewInput;
		if (!onViewInput || !a_state.previewOn) {
			a_state.viewDragButton = -1;
			a_state.viewDragX = 0.0f;
			a_state.viewDragY = 0.0f;
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

	// --- Capture page ---------------------------------------------------------

	void SelectOutfit(State& a_state, int a_index);  // Update page

	std::size_t GroupIndex(CaptureWindow::PieceGroup a_group)
	{
		return static_cast<std::size_t>(a_group);
	}

	// The headings over the groups the actor's own pieces fall in; the loaded outfit's
	// (PieceGroup::kNotCarried) follows each Load (LoadOutfit).
	void SetGroupHeadings(State& a_state)
	{
		using Group = CaptureWindow::PieceGroup;
		const auto& request = a_state.request;
		const auto& name = request.actorName;
		auto&       headings = a_state.headings;
		headings[GroupIndex(Group::kLiveryOutfit)] = std::format("From {}'s Outfit (\"{}\")", name, request.outfitName);
		headings[GroupIndex(Group::kWorn)] = std::format("From {}'s Outfit (Not from a Saved Outfit)", name);
		headings[GroupIndex(Group::kCarried)] = std::format("From {}'s Inventory", name);
		headings[GroupIndex(Group::kPlayer)] = "From Player's Inventory";
		headings[GroupIndex(Group::kNotCarried)].clear();
	}

	// The column's order of two pieces: negative, zero or positive.
	int CompareColumn(const CaptureWindow::Piece& a_left, const CaptureWindow::Piece& a_right, int a_column)
	{
		switch (a_column) {
		case kTypeColumn:
			return a_left.typeOrder - a_right.typeOrder;
		case kClassColumn:
			return a_left.classOrder - a_right.classOrder;
		default:
			return 0;  // the name decides below
		}
	}

	// Orders the pieces for the list: by group, as the headings go, then by the sorted
	// column (Type and Class in SkyUI's order), the name breaking ties, all of it reversed
	// while descending. Run at Open and on a click on a column label, never per frame.
	void SortPieces(State& a_state)
	{
		const auto& pieces = a_state.request.pieces;
		auto&       order = a_state.pieceOrder;
		order.resize(pieces.size());
		for (std::size_t i = 0; i < order.size(); ++i) {
			order[i] = i;
		}
		std::ranges::sort(order, [&](std::size_t a_left, std::size_t a_right) {
			const auto& left = pieces[a_left];
			const auto& right = pieces[a_right];
			if (left.group != right.group) {
				return left.group < right.group;
			}
			int compared = CompareColumn(left, right, a_state.sortColumn);
			if (compared == 0) {
				compared = _stricmp(left.name.c_str(), right.name.c_str());
			}
			if (compared == 0) {
				return a_left < a_right;
			}
			return a_state.sortDescending ? compared > 0 : compared < 0;
		});
	}

	// Name takes what Type and Class leave at the list's right end, each of them as wide
	// as its widest text, or its label with room for the sort arrow.
	std::array<float, 3> ColumnPositions(const State& a_state)
	{
		const float labelRoom = ImGui::GetFontSize();  // the small-font label's arrow and its gap fit in a body-font em
		float       typeWidth = ImGui::CalcTextSize(kColumnLabels[kTypeColumn]).x + labelRoom;
		float       classWidth = ImGui::CalcTextSize(kColumnLabels[kClassColumn]).x + labelRoom;
		for (const auto& piece : a_state.request.pieces) {
			typeWidth = (std::max)(typeWidth, ImGui::CalcTextSize(piece.type.c_str()).x);
			classWidth = (std::max)(classWidth, ImGui::CalcTextSize(piece.armorClass.c_str()).x);
		}
		const float classX = (std::max)(0.0f, Theme::ColumnsWidth() - classWidth);
		const float typeX = (std::max)(0.0f, classX - Theme::ColumnGap() - typeWidth);
		return { 0.0f, typeX, classX };
	}

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

	// The groups that have a listed piece, so their headings show.
	std::size_t CountListedGroups(const State& a_state)
	{
		std::array<bool, kPieceGroupCount> listed{};
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			if (IsListed(a_state, i)) {
				listed[GroupIndex(a_state.request.pieces[i].group)] = true;
			}
		}
		return static_cast<std::size_t>(std::ranges::count(listed, true));
	}

	// A group's heading inside the list: greyed, in the small font, left of the names.
	void DrawGroupHeading(const std::string& a_heading)
	{
		Theme::PushSmallFont();
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		Theme::TextRow(a_heading.c_str());
		ImGui::PopStyleColor();
		Theme::PopSmallFont();
	}

	// Height of the piece list with every listed piece and heading in view, capped as
	// ListHeight caps it. Contract: called with the list's row spacing pushed.
	float PieceListHeight(const State& a_state)
	{
		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		Theme::PushSmallFont();
		const float headingHeight = Theme::SelectRowHeight();
		Theme::PopSmallFont();
		const float rowsHeight = static_cast<float>(CountListedPieces(a_state)) * (ImGui::GetFrameHeight() + spacing) +
		                         static_cast<float>(CountListedGroups(a_state)) * (headingHeight + spacing);
		return FittedListHeight(rowsHeight, ImGui::GetFrameHeightWithSpacing());
	}

	// The column labels, then the listed pieces under their groups' headings, a tick row
	// each; unticked pieces are left out of the outfit. A click on a label sorts by its
	// column, a second one reverses the order. Each tick goes to onPieceToggled (the live
	// preview) in its own SKSE task, queued in click order.
	// Contract: a piece that is not listed is never ticked: only Load ticks a piece
	// nobody carries, and the next Load sets every tick again.
	void DrawPieceRows(State& a_state)
	{
		const auto& pieces = a_state.request.pieces;
		const auto& onToggled = a_state.request.onPieceToggled;
		const auto  columnX = ColumnPositions(a_state);
		if (const int clicked = Theme::HeaderRow(kColumnLabels, columnX, a_state.sortColumn, a_state.sortDescending); clicked >= 0) {
			QueueMenuSound(kFocusSound);
			a_state.sortDescending = clicked == a_state.sortColumn && !a_state.sortDescending;
			a_state.sortColumn = clicked;
			SortPieces(a_state);
		}
		PushListRowSpacing();  // popped before EndList, so the gap below the list is the normal one
		if (Theme::BeginList("pieces", PieceListHeight(a_state))) {
			std::size_t group = kPieceGroupCount;  // none drawn yet
			for (const auto i : a_state.pieceOrder) {
				if (!IsListed(a_state, i)) {
					continue;
				}
				if (GroupIndex(pieces[i].group) != group) {
					group = GroupIndex(pieces[i].group);
					DrawGroupHeading(a_state.headings[group]);
				}
				ImGui::PushID(static_cast<int>(i));
				const std::array<const char*, 3> cells{ pieces[i].name.c_str(), pieces[i].type.c_str(), pieces[i].armorClass.c_str() };
				if (Theme::TickRow(cells, columnX, &a_state.pieceTicked[i])) {
					QueueMenuSound(kFocusSound);
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
		ImGui::PopStyleVar();
		Theme::EndList();
	}

	// The count line under the list, ending with Load Outfit..., greyed with the reason on
	// hover while the library has no outfits; returns true when it is clicked.
	bool DrawPieceCount(const State& a_state)
	{
		Theme::LabelText(std::format("{} of {} piece(s) selected", CountTickedPieces(a_state), CountListedPieces(a_state)).c_str());
		const bool noOutfits = a_state.request.existingOutfits.empty();
		ImGui::BeginDisabled(noOutfits);
		const bool load = Theme::LabelLink("Load Outfit...") && !noOutfits;
		const bool loadHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		// After EndDisabled, so the tooltip is not drawn faded.
		if (noOutfits && loadHovered) {
			ImGui::SetTooltip("%s", kNoOutfits);
		}
		if (CountTickedPieces(a_state) == 0) {
			ImGui::TextDisabled(kNeedPiece);
		}
		return load;
	}

	// Turns the live preview on or off and the game's freeze the other way.
	// Turning it on puts the window back on its start place beside the preview,
	// unless the user moved it; a refusal greys the tick with the reason.
	// Contract: game thread (SKSE task queued by DrawPreviewToggle). Open and
	// Close run only there too, so the session cannot change between the two
	// locks; the handlers run outside the lock, as they dress the actor.
	void ApplyPreviewToggle(unsigned a_session, bool a_on, const std::vector<std::size_t>& a_pieces)
	{
		auto&                                                       state = GetState();
		std::function<std::string(const std::vector<std::size_t>&)> onPreviewOn;
		std::function<void()>                                       onPreviewOff;
		{
			const std::scoped_lock lock(state.mutex);
			if (state.status == Status::kClosed || state.session != a_session) {
				return;
			}
			onPreviewOn = state.request.onPreviewOn;
			onPreviewOff = state.request.onPreviewOff;
		}
		std::string reason;
		if (a_on) {
			reason = onPreviewOn(a_pieces);
		} else {
			onPreviewOff();
		}
		const bool on = a_on && reason.empty();
		{
			const std::scoped_lock lock(state.mutex);
			state.previewBusy = false;
			state.previewOn = on;
			state.request.previewNote = std::move(reason);
			state.frozeGame = !on;
			if (on && !state.userMoved) {
				state.placeCentre.reset();  // the start place
				state.recentreFrames = kRecentreFrames;
			}
		}
		SetGameFrozen(!on);
	}

	// The Live Preview help on hover of its "(?)": what the preview does, then
	// the camera controls. One item per line, so the gaps between lines can be
	// set: a newline inside one text keeps the font's own line height.
	void DrawPreviewHelpTooltip()
	{
		constexpr const char*                kSummary = "Keeps the game running and dresses the character in the ticked pieces.";
		constexpr std::array<const char*, 5> kControls{
			"With the cursor over the game:",
			"Left drag - turns around the character",
			"Right drag - pans",
			"Mouse wheel - zooms",
			"W / A / S / D - moves (hold Shift for half speed)",
		};
		constexpr float kLineGap = 2.0f;     // pixels at 1080p, between lines
		constexpr float kSummaryGap = 8.0f;  // pixels at 1080p, between the summary and the controls

		ImGui::BeginTooltip();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kLineGap * Theme::Scale()));
		ImGui::TextUnformatted(kSummary);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (kSummaryGap - kLineGap) * Theme::Scale());
		for (const char* line : kControls) {
			ImGui::TextUnformatted(line);
		}
		ImGui::PopStyleVar();
		ImGui::EndTooltip();
	}

	// The Live Preview tick: greyed, with the reason on hover, when the preview
	// cannot be on; each click queues ApplyPreviewToggle with the ticked pieces
	// of that moment, so the piece clicks queued after it apply on top. A "(?)"
	// beside it explains the preview and its camera controls on hover.
	void DrawPreviewToggle(State& a_state)
	{
		const auto& note = a_state.request.previewNote;
		const bool  available = note.empty() && a_state.request.onPreviewOn && a_state.request.onPreviewOff;
		bool        on = a_state.previewOn;
		ImGui::BeginDisabled(!available || a_state.previewBusy);
		Theme::SectionGap();  // a field of its own, with no label
		const bool clicked = Theme::Checkbox("Live Preview", &on);
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		// After EndDisabled, so the tooltip is not drawn faded.
		if (hovered && !note.empty()) {
			ImGui::SetTooltip("%s", note.c_str());
		}
		// Outside BeginDisabled, so the help stays readable while the tick is greyed.
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) {
			DrawPreviewHelpTooltip();
		}
		if (clicked && available && !a_state.previewBusy) {
			QueueMenuSound(kFocusSound);
			a_state.previewBusy = true;
			SKSE::GetTaskInterface()->AddTask(
				[session = a_state.session, on, pieces = TickedPieces(a_state)] { ApplyPreviewToggle(session, on, pieces); });
		}
	}

	// The id of the outfit last loaded when the ticks are exactly its pieces, else 0.
	std::uint32_t TickedOutfitId(const State& a_state)
	{
		if (a_state.loadedOutfit < 0) {
			return 0;
		}
		const auto& outfit = OutfitAt(a_state, a_state.loadedOutfit);
		return TickedPieces(a_state) == outfit.pieces ? outfit.id : 0;
	}

	// The widest button row of the Capture page, which sets the page's least width.
	float CaptureButtonsWidth()
	{
		return ButtonRowWidth({ "Save", "Update Existing", "Apply", "Cancel" });
	}

	// A help text in a tooltip, wrapped to a readable width.
	void DrawHelpTooltip(const std::string& a_text)
	{
		constexpr float kWrapEms = 28.0f;  // the tooltip's text width, in font sizes

		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * kWrapEms);
		ImGui::TextUnformatted(a_text.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	// Restore Original Outfit and its "(?)", centred under the buttons, while Livery
	// dresses the character (Request::onRestore); returns true when it is clicked.
	bool DrawRestoreRow(const State& a_state)
	{
		constexpr const char* kLabel = "Restore Original Outfit";
		constexpr const char* kHelpMark = "(?)";

		if (!a_state.request.onRestore) {
			return false;
		}
		const float width = ImGui::CalcTextSize(kLabel).x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(kHelpMark).x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
		const bool clicked = Theme::TextButton(kLabel);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", kHelpMark);
		if (ImGui::IsItemHovered()) {
			const auto& name = a_state.request.actorName;
			DrawHelpTooltip(std::format(
				"Takes Livery's outfit off {0}: its locks go, the pieces it added are taken back, the clothes it took off go back on, "
				"and its situation choices are forgotten. Items moved from your inventory stay with {0}. A slot whose own piece is "
				"gone is left to the game, which may take a cell change or two to fill it.",
				name));
		}
		return clicked;
	}

	enum class CaptureAction
	{
		kNone,
		kLoadPage,
		kNamePage,
		kUpdatePage,
		kApply,
		kRestore,
		kCancel
	};

	// Save / Update Existing / Apply / Cancel centred below a gap, the Restore row under
	// them; `a_load` is a click on the count line's Load Outfit... (DrawPieceCount). Save,
	// Update Existing and Apply act on the ticked pieces, so they need at least one.
	CaptureAction DrawCaptureButtons(const State& a_state, bool a_load)
	{
		BeginButtonRow({ "Save", "Update Existing", "Apply", "Cancel" });
		const char* noOutfits = a_state.request.existingOutfits.empty() ? kNoOutfits : nullptr;
		const char* noPiece = CountTickedPieces(a_state) == 0 ? kNeedPiece : nullptr;
		const bool  save = ReasonButton("Save", noPiece);
		ImGui::SameLine();
		const bool update = ReasonButton("Update Existing", noPiece ? noPiece : noOutfits);
		ImGui::SameLine();
		const bool apply = ReasonButton("Apply", noPiece);
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		const bool restore = DrawRestoreRow(a_state);

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Applying...");
			return CaptureAction::kNone;
		}
		if (a_load) {
			return CaptureAction::kLoadPage;
		}
		if (save) {
			return CaptureAction::kNamePage;
		}
		if (update) {
			return CaptureAction::kUpdatePage;
		}
		if (apply) {
			return CaptureAction::kApply;
		}
		if (restore) {
			return CaptureAction::kRestore;
		}
		return cancel ? CaptureAction::kCancel : CaptureAction::kNone;
	}

	void RequestApply(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onApply = a_state.request.onApply;
		SKSE::GetTaskInterface()->AddTask([onApply, pieces = TickedPieces(a_state), outfit = TickedOutfitId(a_state)] { onApply(pieces, outfit); });
	}

	void RequestRestore(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onRestore = a_state.request.onRestore;
		SKSE::GetTaskInterface()->AddTask([onRestore] { onRestore(); });
	}

	void DrawCapturePage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		DrawPieceRows(a_state);
		const bool load = DrawPieceCount(a_state);
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		DrawPreviewToggle(a_state);
		const auto action = DrawCaptureButtons(a_state, load);
		EndButtonRow();
		ImGui::EndDisabled();

		switch (action) {
		case CaptureAction::kLoadPage:
			QueueMenuSound(kOkSound);
			a_state.previewedOutfit = -1;
			GoToPage(a_state, Page::kLoad);
			break;
		case CaptureAction::kNamePage:
			QueueMenuSound(kOkSound);
			GoToPage(a_state, Page::kName);
			break;
		case CaptureAction::kUpdatePage:
			QueueMenuSound(kOkSound);
			if (a_state.loadedOutfit >= 0) {
				SelectOutfit(a_state, a_state.loadedOutfit);
			}
			if (a_state.selectedOutfit >= 0) {
				PreviewOutfitOrTicks(a_state, a_state.selectedOutfit);
			}
			GoToPage(a_state, Page::kUpdate);
			break;
		case CaptureAction::kApply:
			QueueMenuSound(kOkSound);
			RequestApply(a_state);
			break;
		case CaptureAction::kRestore:
			QueueMenuSound(kOkSound);
			RequestRestore(a_state);
			break;
		case CaptureAction::kCancel:
			QueueMenuSound(kCancelSound);
			RequestCancel();
			break;
		case CaptureAction::kNone:
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

	// The ticked names among request.categories[a_begin, a_end).
	std::string CategoryPreview(const State& a_state, const bool* a_ticked, std::size_t a_begin, std::size_t a_end)
	{
		std::vector<std::string> names;
		for (std::size_t i = a_begin; i < a_end; ++i) {
			if (a_ticked[i]) {
				names.push_back(a_state.request.categories[i].name);
			}
		}
		return names.empty() ? "None" : JoinNames(names);
	}

	// One full-width option row: hover fill, a highlighted fill and accent text
	// with the game's ticked checkbox (Theme::DrawTickedBox) or else a check
	// mark on the right when ticked. An InvisibleButton does not
	// close the combo popup, so the user can tick several rows in one go.
	// The hit area also covers half the gap above and below the fill, so rows
	// stacked with no item spacing leave no dead strip between them. Returns
	// true when toggled.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool DrawCategoryRow(const char* a_label, bool& a_ticked)
	{
		const auto&  style = ImGui::GetStyle();
		const float  halfGap = Theme::ListRowGap(style.ItemInnerSpacing.y) * 0.5f;
		const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + halfGap * 2.0f);
		const ImVec2 hitTopLeft = ImGui::GetCursorScreenPos();
		const bool   toggled = ImGui::InvisibleButton(a_label, size);
		if (toggled) {
			QueueMenuSound(kFocusSound);
			a_ticked = !a_ticked;
		}
		const bool hovered = ImGui::IsItemHovered();

		const ImVec2 topLeft(hitTopLeft.x, hitTopLeft.y + halfGap);
		const ImVec2 bottomRight(hitTopLeft.x + size.x, hitTopLeft.y + size.y - halfGap);
		auto*        draw = ImGui::GetWindowDrawList();
		if (a_ticked || hovered) {
			draw->AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32(a_ticked ? ImGuiCol_Header : ImGuiCol_HeaderHovered), style.FrameRounding);
		}
		const ImU32  textColour = ImGui::GetColorU32(a_ticked ? ImGuiCol_CheckMark : ImGuiCol_Text);
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const float  midY = (topLeft.y + bottomRight.y) * 0.5f;
		draw->AddText(ImVec2(topLeft.x + style.FramePadding.x, midY - text.y * 0.5f), textColour, a_label);
		const float right = bottomRight.x - style.FramePadding.x;
		if (a_ticked && !Theme::DrawTickedBox(right, midY)) {
			const float  mark = ImGui::GetFontSize() * 0.25f;  // half width of the check mark
			const ImVec2 points[] = { { right - mark * 2.0f, midY }, { right - mark * 1.2f, midY + mark * 0.8f }, { right, midY - mark * 0.8f } };
			draw->AddPolyline(points, 3, textColour, ImDrawFlags_None, (std::max)(1.0f, ImGui::GetFontSize() * 0.08f));
		}
		return toggled;
	}

	// Multi-select dropdown labelled by its group: a combo whose popup lists
	// request.categories[a_begin, a_end) as rows.
	void DrawCategoryCombo(const State& a_state, std::size_t a_begin, std::size_t a_end, bool* a_ticked)
	{
		const char* group = a_state.request.categories[a_begin].group.c_str();
		Theme::LabelText(group);
		ImGui::PushID(group);
		ImGui::SetNextItemWidth(-FLT_MIN);
		const std::string preview = CategoryPreview(a_state, a_ticked, a_begin, a_end);
		const bool        open = Theme::BeginCombo("##combo", ImGuiComboFlags_HeightLargest);
		if (open) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));  // rows carry their own gap
			for (std::size_t i = a_begin; i < a_end; ++i) {
				ImGui::PushID(static_cast<int>(i));
				DrawCategoryRow(a_state.request.categories[i].name.c_str(), a_ticked[i]);
				ImGui::PopID();
			}
			ImGui::PopStyleVar();
		}
		Theme::EndCombo(open, preview.c_str());
		ImGui::PopID();
	}

	// The class group as one row of checkboxes labelled by its group:
	// request.categories[a_begin, a_end).
	void DrawClassField(const State& a_state, std::size_t a_begin, std::size_t a_end, bool* a_ticked)
	{
		Theme::LabelText(CaptureWindow::kClassGroup);
		for (std::size_t i = a_begin; i < a_end; ++i) {
			if (i > a_begin) {
				ImGui::SameLine();
			}
			if (Theme::Checkbox(a_state.request.categories[i].name.c_str(), &a_ticked[i])) {
				QueueMenuSound(kFocusSound);
			}
		}
	}

	// One field per run of request.categories sharing a group, so a group with
	// no category shows no field: checkboxes for the classes, a dropdown
	// for every other group. a_ticked holds one entry per request.categories.
	void DrawCategoryFields(const State& a_state, const char* a_id, bool* a_ticked)
	{
		const auto& options = a_state.request.categories;
		ImGui::PushID(a_id);
		for (std::size_t begin = 0, end = 0; begin < options.size(); begin = end) {
			for (end = begin + 1; end < options.size() && options[end].group == options[begin].group; ++end) {}
			if (options[begin].group == CaptureWindow::kClassGroup) {
				DrawClassField(a_state, begin, end, a_ticked);
			} else {
				DrawCategoryCombo(a_state, begin, end, a_ticked);
			}
		}
		ImGui::PopID();
	}

	// Returns true when the user asked to save with a valid name. The empty field
	// shows kNeedName; a failed save turns it red with the message on hover, until
	// the name is edited.
	bool DrawNameField(State& a_state)
	{
		Theme::LabelText("Outfit name");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (a_state.focusNameField) {
			ImGui::SetKeyboardFocusHere();
			a_state.focusNameField = false;
		}
		const bool              failed = !a_state.error.empty();
		const Theme::FieldStyle style{ .look = failed ? Theme::FieldLook::kError : Theme::FieldLook::kNormal };
		const bool              enter = Theme::InputText("##name", a_state.name.data(), a_state.name.size(), ImGuiInputTextFlags_EnterReturnsTrue, kNeedName, style);
		if (failed) {
			if (ImGui::IsItemEdited()) {
				a_state.error.clear();
			} else if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", a_state.error.c_str());
			}
		}
		return enter && !Trimmed(a_state.name.data()).empty();
	}

	// The new outfit's gender as one row of radio buttons.
	void DrawGenderField(State& a_state)
	{
		struct GenderOption
		{
			const char* label;
			int         value;  // as the library stores it
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
			if (Theme::RadioButton(kGenderOptions[i].label, a_state.gender == kGenderOptions[i].value)) {
				a_state.gender = kGenderOptions[i].value;
				QueueMenuSound(kFocusSound);
			}
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
		const bool save = ReasonButton("Create", valid ? nullptr : kNeedName) || a_save;
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
		DrawCategoryFields(a_state, "##categories", a_state.ticked.get());
		DrawGenderField(a_state);
		const auto action = DrawNameButtons(a_state, save);
		EndButtonRow();
		ImGui::EndDisabled();

		switch (action) {
		case NameAction::kSave:
			QueueMenuSound(kOkSound);
			RequestSave(a_state);
			break;
		case NameAction::kBack:
			QueueMenuSound(kCancelSound);
			GoToPage(a_state, Page::kCapture);
			break;
		case NameAction::kNone:
			break;
		}
	}

	// --- Outfit browser -------------------------------------------------------

	struct BrowserInput
	{
		bool changed = false;      // the search, a filter tick or the sort changed: refresh the list
		bool escapeTaken = false;  // Escape cleared the search, so it is not also Back
	};

	// Top of the browser: an underlined field with a magnifying glass, its rule
	// in the accent while text is typed. Escape with text typed clears it, the
	// field active or not (EscapeClearsAll does it while active); with none
	// typed it is Back.
	void DrawSearchField(State& a_state, BrowserInput& a_input)
	{
		auto& browser = a_state.browser;
		a_input.escapeTaken = OutfitBrowser::IsSearching(browser) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
		                      !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

		const Theme::FieldStyle style{ .look = OutfitBrowser::IsSearching(browser) ? Theme::FieldLook::kHighlighted : Theme::FieldLook::kNormal,
			.shape = Theme::FieldShape::kUnderlined,
			.icon = Theme::FieldIcon::kSearch };
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::SetNextItemAllowOverlap();  // the clear button sits on its right end
		a_input.changed |= Theme::InputText("##search", browser.search.data(), browser.search.size(), ImGuiInputTextFlags_EscapeClearsAll,
			"Search by outfit name", style);
		bool clear = a_input.escapeTaken;
		if (OutfitBrowser::IsSearching(browser)) {
			const ImVec2 fieldMin = ImGui::GetItemRectMin();
			const ImVec2 fieldMax = ImGui::GetItemRectMax();
			const float  side = fieldMax.y - fieldMin.y;
			ImGui::SetCursorScreenPos(ImVec2(fieldMax.x - side, fieldMin.y));
			if (Theme::ClearButton("##clearSearch", side)) {
				QueueMenuSound(kFocusSound);
				clear = true;
			}
		}
		if (clear) {
			OutfitBrowser::ClearSearch(browser);
			a_input.changed = true;
		}
	}

	// The Filter dropdown's rows under a grey heading per group, lined up with
	// the rows' text.
	void DrawFilterRows(State& a_state, BrowserInput& a_input)
	{
		auto&       browser = a_state.browser;
		const auto& style = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));  // rows carry their own gap
		for (std::size_t i = 0; i < browser.filter.size(); ++i) {
			const auto& row = browser.filter[i];
			if (i == 0 || row.group != browser.filter[i - 1].group) {
				if (i > 0) {
					ImGui::Dummy(ImVec2(0.0f, style.ItemInnerSpacing.y));
				}
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.FramePadding.x);
				ImGui::TextDisabled("%s", row.group.c_str());
			}
			ImGui::PushID(static_cast<int>(i));
			a_input.changed |= DrawCategoryRow(row.label.c_str(), browser.filterTicked[i]);
			ImGui::PopID();
		}
		ImGui::PopStyleVar();
	}

	// Under the search: an underlined multi-select dropdown with a funnel,
	// its rule in the accent while a row is ticked, narrowed by the clear
	// button beside it so the button keeps clear of the skin's arrow. With
	// nothing ticked it shows a greyed hint and the button is greyed out.
	void DrawFilterField(State& a_state, BrowserInput& a_input)
	{
		auto&                   browser = a_state.browser;
		const bool              filtering = OutfitBrowser::IsFiltering(browser);
		const Theme::FieldStyle style{ .look = filtering ? Theme::FieldLook::kHighlighted : Theme::FieldLook::kNormal,
			.shape = Theme::FieldShape::kUnderlined,
			.icon = Theme::FieldIcon::kFilter,
			.previewIsHint = !filtering };
		const float             side = Theme::FieldHeight(style);
		const float             gap = ImGui::GetStyle().ItemInnerSpacing.x;
		ImGui::SetNextItemWidth((std::max)(0.0f, ImGui::GetContentRegionAvail().x - side - gap));
		const std::string preview = filtering ? OutfitBrowser::FilterPreview(browser) : "Filter by category";
		const bool        open = Theme::BeginCombo("##filter", ImGuiComboFlags_HeightLargest, style);
		if (open) {
			DrawFilterRows(a_state, a_input);
		}
		Theme::EndCombo(open, preview.c_str(), style);

		ImGui::SameLine(0.0f, gap);
		ImGui::BeginDisabled(!filtering);
		if (Theme::ClearButton("##clearFilter", side)) {
			QueueMenuSound(kFocusSound);
			OutfitBrowser::ClearFilter(browser);
			a_input.changed = true;
		}
		ImGui::EndDisabled();
	}

	struct BrowserControls
	{
		bool  escapeTaken = false;  // Escape cleared the search, so it is not also Back
		float height = 0.0f;        // pixels the controls took, which the outfit list's height cap leaves room for
	};

	// Search over Filter, in the small font, with no labels, at the top of the
	// outfit list's column; refreshes the list only when one of them changed.
	BrowserControls DrawBrowserControls(State& a_state)
	{
		constexpr float kGapBelowRow = 4.0f;  // pixels at 1080p, under the row on top of the item spacing

		const float top = ImGui::GetCursorPosY();
		Theme::SectionGap();  // the heading leaves its last gap to the page's first field (Theme::HeadingText)
		BrowserInput input;
		ImGui::PushID("browser");
		Theme::PushSmallFont();
		DrawSearchField(a_state, input);
		DrawFilterField(a_state, input);
		Theme::PopSmallFont();
		ImGui::PopID();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kGapBelowRow * Theme::Scale());
		if (input.changed) {
			OutfitBrowser::Refresh(a_state.browser, a_state.request);
		}
		return { input.escapeTaken, ImGui::GetCursorPosY() - top };
	}

	// --- Outfit lists ---------------------------------------------------------

	// The Sort dropdown's rows, the current key ticked; choosing one closes the
	// popup. A zero-high spacer as wide as the widest row keeps the popup,
	// which starts as narrow as its frame, from crowding a row's tick onto its
	// label. Returns true when the key changed.
	bool DrawSortRows(State& a_state)
	{
		const auto& style = ImGui::GetStyle();
		float       widest = 0.0f;
		for (const auto& option : kSortOptions) {
			widest = (std::max)(widest, ImGui::CalcTextSize(option.label).x);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));  // rows carry their own gap
		ImGui::Dummy(ImVec2(widest + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x + ImGui::GetFrameHeight(), 0.0f));
		bool changed = false;
		for (const auto& option : kSortOptions) {
			bool ticked = a_state.browser.sort == option.key;
			if (DrawCategoryRow(option.label, ticked)) {
				changed = a_state.browser.sort != option.key;
				a_state.browser.sort = option.key;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::PopStyleVar();
		return changed;
	}

	// "Sort by <key>" and the skin's arrow, bare, in the small font and greyed
	// as the fields' hints are, at the right end of the label just drawn with
	// its text's bottom on the label's. Its width fits the longest key, so
	// switching keys does not move it. It leaves the cursor where the label
	// did, so the list below does not move.
	// Returns true when the key changed.
	bool DrawSortCombo(State& a_state)
	{
		const ImVec2 labelMax = ImGui::GetItemRectMax();
		const ImVec2 after = ImGui::GetCursorScreenPos();
		const float  right = after.x + ImGui::GetContentRegionAvail().x;
		const auto   preview = [](const SortOption& a_option) { return std::format("Sort by {}", a_option.label); };

		Theme::PushSmallFont();
		constexpr Theme::FieldStyle style{ .shape = Theme::FieldShape::kBare, .previewIsHint = true };
		float                       width = 0.0f;
		std::string                 current;
		for (const auto& option : kSortOptions) {
			width = (std::max)(width, Theme::ComboWidth(preview(option).c_str(), style));
			if (option.key == a_state.browser.sort) {
				current = preview(option);
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(right - width, labelMax.y - Theme::FieldHeight(style)));
		ImGui::SetNextItemWidth(width);
		const bool open = Theme::BeginCombo("##sort", ImGuiComboFlags_None, style);
		const bool changed = open && DrawSortRows(a_state);
		Theme::EndCombo(open, current.c_str(), style);
		Theme::PopSmallFont();
		ImGui::SetCursorScreenPos(after);
		return changed;
	}

	// Left column of the Load and Update pages: the library's outfits that pass
	// the browser, in its order, `a_selected` highlighted, under a label with
	// the Sort dropdown. The list stays as tall as the whole library, so
	// narrowing it does not move the buttons; `a_reserved` is the browser's
	// height (ListHeight). Returns the index into request.existingOutfits of
	// the one clicked, or -1; a click plays the focus sound.
	// Names are shown as they are (Theme::SelectRow).
	int DrawOutfitList(State& a_state, int a_selected, float a_reserved)
	{
		const auto& outfits = a_state.request.existingOutfits;
		const auto& visible = a_state.browser.visible;
		if (visible.size() == outfits.size()) {
			Theme::LabelText(std::format("{} outfit(s) in the library", outfits.size()).c_str());
		} else {
			Theme::LabelText(std::format("{} of {} outfits", visible.size(), outfits.size()).c_str());
		}
		if (DrawSortCombo(a_state)) {
			OutfitBrowser::Refresh(a_state.browser, a_state.request);
		}
		const float gap = Theme::SelectRowGap();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, gap));
		const float listHeight = ListHeight(outfits.size(), Theme::SelectRowHeight() + gap, a_reserved);
		int         clicked = -1;
		if (Theme::BeginList("outfits", listHeight)) {
			if (visible.empty()) {
				ImGui::TextDisabled("No outfit matches.");
			}
			for (const int index : visible) {
				ImGui::PushID(index);
				if (Theme::SelectRow(OutfitAt(a_state, index).name.c_str(), a_selected == index)) {
					clicked = index;
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
		if (clicked >= 0) {
			QueueMenuSound(kFocusSound);
		}
		return clicked;
	}

	// Top of the right column, level with the search field: the selected
	// outfit's name, so the selection stays named while the browser hides it
	// from the list. With none selected it shows `a_hint` as a field label and
	// returns false.
	bool DrawSelectedName(const State& a_state, int a_selected, const char* a_hint)
	{
		if (a_selected < 0) {
			Theme::LabelText(a_hint);
			return false;
		}
		Theme::SectionGap();  // as the label beside it opens (Theme::LabelText)
		ImGui::TextWrapped("%s", OutfitAt(a_state, a_selected).name.c_str());
		return true;
	}

	// The outfit's pieces as the library had them when the window opened, in rows
	// laid out as the outfit list's beside it. The count includes the pieces
	// whose plugin is not loaded, which have no row.
	void DrawOutfitPieces(const State& a_state, const CaptureWindow::ExistingOutfit& a_outfit)
	{
		const auto total = a_outfit.pieces.size() + a_outfit.unloadedPieces;
		const auto count = a_outfit.unloadedPieces ? std::format("{} piece(s), {} not loaded", total, a_outfit.unloadedPieces) :
		                                             std::format("{} piece(s)", total);
		Theme::LabelText(count.c_str());
		const float gap = Theme::SelectRowGap();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, gap));
		const float listHeight = ListHeight(a_outfit.pieces.size(), Theme::SelectRowHeight() + gap);
		if (Theme::BeginList("outfitPieces", listHeight)) {
			for (const auto index : a_outfit.pieces) {
				Theme::TextRow(a_state.request.pieces[index].name.c_str());
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
	}

	// --- Load page ------------------------------------------------------------

	// Right column: the previewed outfit's name, categories and pieces, read only.
	void DrawLoadDetails(const State& a_state)
	{
		if (!DrawSelectedName(a_state, a_state.previewedOutfit, "Select an outfit to load")) {
			return;
		}
		Theme::LabelText("Categories");
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

	// Load / Back centred below a gap. a_escapeTaken: the browser used this
	// frame's Escape. An outfit with no loaded piece would tick nothing.
	LoadAction DrawLoadButtons(const State& a_state, bool a_escapeTaken)
	{
		const char* noLoad = kNeedOutfit;
		if (a_state.previewedOutfit >= 0) {
			noLoad = OutfitAt(a_state, a_state.previewedOutfit).pieces.empty() ? kNoLoadedPiece : nullptr;
		}
		BeginButtonRow({ "Load", "Back" });
		const bool load = ReasonButton("Load", noLoad);
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken);

		if (load) {
			return LoadAction::kLoad;
		}
		return back ? LoadAction::kBack : LoadAction::kNone;
	}

	// Ticks exactly the previewed outfit's pieces, which the preview already
	// shows; those nobody carries stay listed, under its heading, while it is loaded.
	void LoadOutfit(State& a_state)
	{
		const auto& outfit = OutfitAt(a_state, a_state.previewedOutfit);
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			a_state.pieceTicked[i] = HasPiece(outfit, i);
		}
		a_state.loadedOutfit = a_state.previewedOutfit;
		a_state.headings[GroupIndex(CaptureWindow::PieceGroup::kNotCarried)] = std::format("From \"{}\" (Not Carried)", outfit.name);
	}

	// Selecting an outfit shows it on the preview; a second click on it, or Back,
	// shows the ticks again. Nothing here changes the ticks until Load.
	void DrawLoadPage(State& a_state)
	{
		BrowserControls controls;
		int             clicked = -1;
		if (BeginColumns("outfits")) {
			ImGui::TableNextColumn();
			controls = DrawBrowserControls(a_state);
			clicked = DrawOutfitList(a_state, a_state.previewedOutfit, controls.height);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			DrawLoadDetails(a_state);
			EndColumns();
		}
		if (clicked >= 0) {
			a_state.previewedOutfit = clicked == a_state.previewedOutfit ? -1 : clicked;
			PreviewOutfitOrTicks(a_state, a_state.previewedOutfit);
		}

		const auto action = DrawLoadButtons(a_state, controls.escapeTaken);
		EndButtonRow();
		switch (action) {
		case LoadAction::kLoad:
			QueueMenuSound(kOkSound);
			LoadOutfit(a_state);
			GoToPage(a_state, Page::kCapture);
			break;
		case LoadAction::kBack:
			QueueMenuSound(kCancelSound);
			if (a_state.previewedOutfit >= 0) {
				QueuePreview(a_state, TickedPieces(a_state));
			}
			GoToPage(a_state, Page::kCapture);
			break;
		case LoadAction::kNone:
			break;
		}
	}

	// --- Update page ----------------------------------------------------------

	// Starts the category ticks from the outfit's own categories.
	void SelectOutfit(State& a_state, int a_index)
	{
		if (a_state.selectedOutfit == a_index) {
			return;  // keep the user's ticks
		}
		a_state.selectedOutfit = a_index;
		const auto& joined = a_state.request.existingOutfits[static_cast<std::size_t>(a_index)].categories;
		const auto& options = a_state.request.categories;
		for (std::size_t i = 0; i < options.size(); ++i) {
			a_state.updateTicked[i] = std::ranges::find(joined, options[i].name) != joined.end();
		}
	}

	// The categories ticked on the Update page, which replace the selected outfit's.
	std::vector<std::string> UpdateCategories(const State& a_state)
	{
		std::vector<std::string> ticked;
		for (std::size_t i = 0; i < a_state.request.categories.size(); ++i) {
			if (a_state.updateTicked[i]) {
				ticked.push_back(a_state.request.categories[i].name);
			}
		}
		return ticked;
	}

	// Right column: the selected outfit as the library had it when the window
	// opened, with the category fields ticked as it has them.
	void DrawOutfitDetails(State& a_state)
	{
		if (!DrawSelectedName(a_state, a_state.selectedOutfit, "Select an outfit to update")) {
			return;
		}
		DrawCategoryFields(a_state, "##updateCategories", a_state.updateTicked.get());
		DrawOutfitPieces(a_state, OutfitAt(a_state, a_state.selectedOutfit));
	}

	// Update / Back centred below a gap; Update only opens the confirm popup.
	// Returns true when the user asked to go back to the Capture page.
	// a_escapeTaken: the browser used this frame's Escape.
	// Contract: called in the window's own ID scope, as DrawConfirmPopup is, so
	// OpenPopup and BeginPopupModal name the same popup.
	bool DrawUpdateButtons(State& a_state, bool a_escapeTaken)
	{
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		BeginButtonRow({ "Update", "Back" });
		if (ReasonButton("Update", a_state.selectedOutfit >= 0 ? nullptr : kNeedOutfit)) {
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kConfirmPopup);
		}
		ImGui::SameLine();
		// While the popup is open, Escape is its own Cancel.
		const bool back = Theme::Button("Back") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken && !ImGui::IsPopupOpen(kConfirmPopup));

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Saving...");
			return false;
		}
		return back;
	}

	// Returns true when the user confirmed replacing the selected outfit's pieces
	// and categories with the ticked ones.
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
		if (outfit.unloadedPieces) {
			ImGui::TextDisabled("%s", std::format("{} piece(s) from unloaded plugins stay in it.", outfit.unloadedPieces).c_str());
		}
		if (const auto categories = UpdateCategories(a_state); categories.empty()) {
			ImGui::TextUnformatted("It will be in no category.");
		} else {
			ImGui::TextUnformatted(std::format("Its categories become: {}.", JoinNames(categories)).c_str());
		}
		BeginButtonRow({ "Confirm", "Cancel" });
		const bool confirm = Theme::Button("Confirm");
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (confirm || cancel) {
			QueueMenuSound(confirm ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return confirm;
	}

	void RequestUpdate(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		const auto id = a_state.request.existingOutfits[static_cast<std::size_t>(a_state.selectedOutfit)].id;
		const auto pieces = TickedPieces(a_state);
		const auto categories = UpdateCategories(a_state);
		auto       onUpdate = a_state.request.onUpdate;
		SKSE::GetTaskInterface()->AddTask([onUpdate, id, pieces, categories] { onUpdate(id, pieces, categories); });
	}

	// Selecting an outfit shows it on the preview; a second click on it, or Back,
	// shows the ticks again.
	void DrawUpdatePage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		BrowserControls controls;
		if (BeginColumns("outfits")) {
			ImGui::TableNextColumn();
			controls = DrawBrowserControls(a_state);
			if (const int clicked = DrawOutfitList(a_state, a_state.selectedOutfit, controls.height); clicked >= 0) {
				if (clicked == a_state.selectedOutfit) {
					a_state.selectedOutfit = -1;
				} else {
					SelectOutfit(a_state, clicked);
				}
				PreviewOutfitOrTicks(a_state, a_state.selectedOutfit);
			}
			ImGui::TableNextColumn();
			ResetCellBaseline();
			DrawOutfitDetails(a_state);
			EndColumns();
		}
		const bool back = DrawUpdateButtons(a_state, controls.escapeTaken);
		EndButtonRow();
		ImGui::EndDisabled();

		if (DrawConfirmPopup(a_state)) {
			RequestUpdate(a_state);
		} else if (back) {
			QueueMenuSound(kCancelSound);
			if (a_state.selectedOutfit >= 0) {
				QueuePreview(a_state, TickedPieces(a_state));
			}
			GoToPage(a_state, Page::kCapture);
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
			state.gender = state.request.suggestedGender;
			state.error.clear();
			state.pending.clear();
			state.focusNameField = false;
			state.status = Status::kEditing;
			state.resetImGui = true;
			state.page = Page::kCapture;
			state.captureTitle = std::format("{}'s Inventory", state.request.actorName);
			SetGroupHeadings(state);
			state.sortColumn = kNameColumn;
			state.sortDescending = false;
			SortPieces(state);
			state.selectedOutfit = -1;
			state.previewedOutfit = -1;
			state.loadedOutfit = -1;
			OutfitBrowser::Reset(state.browser, state.request);
			state.recentreFrames = kRecentreFrames;
			state.placeCentre.reset();  // the start place
			state.userMoved = false;
			++state.session;
			state.previewOn = state.request.previewOn;
			state.previewBusy = false;
			state.viewDragButton = -1;
			state.viewDragX = 0.0f;
			state.viewDragY = 0.0f;
			// A live preview needs the game running: the engine applies an NPC's
			// queued equips in the actor update, which a frozen game skips.
			state.frozeGame = !state.previewOn;
		}
		g_open = true;
		if (state.frozeGame) {
			SetGameFrozen(true);
		}
		Hooks::ShowMenu();
		QueueMenuSound(kOpenSound);
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
			state.pending.clear();
		}
		g_open = false;  // before HideMenu: the menu's hide handler tells our hide from the game's by this
		if (frozeGame) {
			SetGameFrozen(false);
		}
		Hooks::HideMenu();
		if (onClose) {
			onClose();  // outside the lock: it dresses the actor again
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

		// The width follows the screen height, as the font does, so an ultrawide screen does not stretch the window.
		constexpr float kWindowWidthRatio = 0.80f;  // of the screen height, for the two-column Load and Update pages
		constexpr float kNarrowWidthRatio = 0.53f;  // of the screen height, for the one-column pages
		const bool      twoColumns = state.page == Page::kLoad || state.page == Page::kUpdate;
		float           width = io.DisplaySize.y * (twoColumns ? kWindowWidthRatio : kNarrowWidthRatio);
		if (state.page == Page::kCapture) {
			width = (std::max)(width, CaptureButtonsWidth() + ImGui::GetStyle().WindowPadding.x * 2.0f);  // a large font's buttons still fit
		}
		const bool pinned = PlaceWindow(state, io, width);
		// Width is fixed every frame; the height follows the content (a zero
		// axis is left to AlwaysAutoResize), so the window never needs to scroll.
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
		if (!Theme::HasTitleBar()) {
			flags |= ImGuiWindowFlags_NoTitleBar;
		}
		if (!ImGui::Begin("Livery", nullptr, flags)) {
			ImGui::End();
			return;
		}
		// Unpinned, only a drag moves the window: auto-resize keeps its top-left corner.
		const ImVec2 pos = ImGui::GetWindowPos();
		if (!pinned && (pos.x != state.windowPos.x || pos.y != state.windowPos.y)) {
			state.userMoved = true;
		}
		state.windowPos = pos;
		state.windowSize = ImGui::GetWindowSize();
		Theme::DecorateWindow();

		const auto& title = state.page == Page::kCapture ? state.captureTitle : state.request.actorName;
		Theme::HeadingText(title.c_str(), state.request.actorSummary.c_str());
		switch (state.page) {
		case Page::kCapture:
			DrawCapturePage(state);
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
