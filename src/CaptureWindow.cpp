// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State        - the one State (CaptureWindowInternal.h, with the constants), the game freeze,
//                  menu sounds, text trimming
//   Game thread  - Open / Close / SetError (also freeze and unfreeze the game while Live Preview is off)
//   Input        - QueueInput translates engine events and moves the game's cursor;
//                  FeedInput hands them and the cursor position to ImGui
//   Draw         - page switching, shared list and button-row helpers; the tab column
//                  (the Dress page, the Wardrobe page, the Situations page, the Chests page while
//                  Request::offersChests, Appearance) and its rule beside those pages; leaving a
//                  page with unsaved edits through the unsaved-changes alert (Save Changes,
//                  Discard Changes, Back); mouse and key input over the game goes to onViewInput
//   Capture page - the Dress page: the actor's pieces under a heading per group (their
//                  Livery outfit, other worn, carried, the player's, the loaded outfit's or
//                  chest's), in Name / Type / Slot (Settings) columns sorted by a click on a
//                  column label (the piece list the Edit page shows too, PieceList), each tick
//                  handed to onPieceToggled; under the list the count line, ending in Add from
//                  Wardrobe..., with Add from Chest... on the line below (the Load page; each
//                  greyed while it has nothing to offer); the
//                  Live Preview tick, handed to onPreviewOn / onPreviewOff in an SKSE task that
//                  also freezes or unfreezes the game; Save / Apply / Exit, with Restore
//                  at the row's right end while Livery dresses the actor; Apply leaves off
//                  the unticked pieces of the library outfit Livery has on the actor, and asks
//                  first when the ticks hold others (Yes updates it); Apply and Restore hand
//                  over to onApply / onRestore
//   Save page    - Create New Outfit / Update Existing Outfit tabs, each with its own name:
//                  Create shows the ticked pieces read only and an icon they suggest until
//                  one is chosen (CreateIcon, Choose icon), Create and Apply gives the new
//                  outfit the row the Wardrobe page's Apply would (its popup first when that asks);
//                  Update shows the wardrobe's outfits, the chosen one's pieces and the ticked
//                  ones in three columns (ChooseOutfit), its confirm popup before the write;
//                  both tabs hand over to onSave
//   Appearance   - Window skin, Restore original palette and Play menu sounds, written to the
//                  INI at once (Settings); the window is drawn in a new skin from the next frame
//   Outfit pages - the outfit browser and lists, the Load, the Wardrobe page and Edit pages:
//                  OutfitPages.cpp; the Situations page: SituationPage.cpp; the Chests page and
//                  the chest editor: ChestPages.cpp
//   Menu sounds  - the game's menu sounds on Open and on clicks, unless turned off in Settings
#include "PCH.h"

#include "CaptureWindow.h"

#include "CaptureWindowInternal.h"
#include "OutfitBrowser.h"
#include "Settings.h"
#include "sarrai/Overlay.h"
#include "sarrai/Theme.h"

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
#include <span>
#include <utility>

namespace CaptureWindow::Internal
{
	// --- Constants ------------------------------------------------------------

	// The Save page's tabs, Create first, then Update: its heading (DrawSaveTabs), which sets its
	// least width.
	constexpr std::array<const char*, 2> kSaveTabs{ "Create New Outfit", "Update Existing Outfit" };

	// --- State ----------------------------------------------------------------

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

	// The window's next size is centred where the current one is (the user may have moved
	// it); PlaceWindow keeps it inside the screen.
	void HoldCentre(State& a_state)
	{
		a_state.placeCentre = ImVec2(a_state.windowPos.x + a_state.windowSize.x * 0.5f, a_state.windowPos.y + a_state.windowSize.y * 0.5f);
		a_state.recentreFrames = kRecentreFrames;
	}

	void GoToPage(State& a_state, Page a_page)
	{
		a_state.page = a_page;
		a_state.error.clear();
		a_state.focusNameField = a_page == Page::kSave;
		HoldCentre(a_state);
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

	const CaptureWindow::ExistingOutfit& OutfitAt(const State& a_state, int a_index)
	{
		return a_state.request.existingOutfits[static_cast<std::size_t>(a_index)];
	}

	bool HasPiece(const CaptureWindow::ExistingOutfit& a_outfit, std::size_t a_piece)
	{
		return std::ranges::binary_search(a_outfit.pieces, a_piece);
	}

	// Index into request.existingOutfits of library outfit `a_id`; -1 for none or 0.
	int IndexOfOutfit(const State& a_state, std::uint32_t a_id)
	{
		const auto& outfits = a_state.request.existingOutfits;
		const auto  found = std::ranges::find(outfits, a_id, &CaptureWindow::ExistingOutfit::id);
		return a_id != 0 && found != outfits.end() ? static_cast<int>(found - outfits.begin()) : -1;
	}

	// The Dress page's list: its ticks, the pieces nobody carries that a Load listed
	// (State::inventoryShown) under the heading naming where they came from, with the chest
	// icon while a chest was loaded last.
	PieceList InventoryPieces(const State& a_state)
	{
		return { a_state.pieceTicked.get(), &a_state.inventoryShown, &a_state.headings[GroupIndex(CaptureWindow::PieceGroup::kNotCarried)],
			!a_state.inventorySources.empty() && a_state.inventorySources.back().chest ? Theme::HeadingIcon::kChest : Theme::HeadingIcon::kBareForm };
	}

	void SetInventoryShown(State& a_state)
	{
		auto&                    shown = a_state.inventoryShown;
		std::vector<std::string> names;
		shown.clear();
		for (const auto& source : a_state.inventorySources) {
			shown.insert(shown.end(), source.pieces.begin(), source.pieces.end());
			names.push_back(source.name);
		}
		std::ranges::sort(shown);
		shown.erase(std::ranges::unique(shown).begin(), shown.end());
		a_state.headings[GroupIndex(CaptureWindow::PieceGroup::kNotCarried)] = EditShownHeading(names);
	}

	std::vector<std::size_t> Ticked(const State& a_state, const PieceList& a_list)
	{
		std::vector<std::size_t> ticked;
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			if (a_list.ticked[i]) {
				ticked.push_back(i);
			}
		}
		return ticked;
	}

	// The Dress page's ticks, which Save and Apply act on.
	std::vector<std::size_t> TickedPieces(const State& a_state)
	{
		return Ticked(a_state, InventoryPieces(a_state));
	}

	std::size_t CountTickedPieces(const State& a_state)
	{
		return TickedPieces(a_state).size();
	}

	// Pieces someone carries, and the list's other ones.
	bool IsListed(const State& a_state, const PieceList& a_list, std::size_t a_piece)
	{
		return a_state.request.pieces[a_piece].group != CaptureWindow::PieceGroup::kNotCarried ||
		       (a_list.shown && std::ranges::binary_search(*a_list.shown, a_piece));
	}

	std::size_t CountListedPieces(const State& a_state, const PieceList& a_list)
	{
		std::size_t listed = 0;
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			listed += IsListed(a_state, a_list, i) ? 1 : 0;
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
	// `a_minHeight`: a child of height zero would fill the window. Whole pixels, rounded up:
	// ImGui truncates the child's inner height but not its padding, so a fractional height
	// (Modern's row gap above 1080p) a third of a pixel short of the rows shows a scroll bar.
	// Contract: called with the item spacing the rows will be laid out with.
	float FittedListHeight(float a_rowsHeight, float a_minHeight, float a_reserved)
	{
		const float fullHeight = a_rowsHeight - ImGui::GetStyle().ItemSpacing.y + Theme::ListPaddingY() * 2.0f;
		const float cap = ImGui::GetIO().DisplaySize.y * kMaxListHeightRatio - a_reserved;
		return std::ceil((std::max)(a_minHeight, (std::min)(fullHeight, cap)));
	}

	// FittedListHeight for rows of one height; an empty list keeps one row.
	float ListHeight(std::size_t a_rows, float a_rowHeight, float a_reserved)
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
	// Theme::Button otherwise, with `a_tooltip` (if any) on hover. Returns true when
	// clicked while enabled.
	bool ReasonButton(const char* a_label, const char* a_reason, const char* a_tooltip)
	{
		ImGui::BeginDisabled(a_reason != nullptr);
		const bool clicked = Theme::Button(a_label);
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		// After EndDisabled, so the tooltip is not drawn faded.
		const char* tooltip = a_reason ? a_reason : a_tooltip;
		if (tooltip && hovered) {
			Theme::Tooltip(tooltip);
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

	// The cursor placed so a row of buttons with these labels is centred in the room left on
	// the line.
	void CentreButtonRow(std::initializer_list<const char*> a_labels)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - ButtonRowWidth(a_labels)) * 0.5f));
	}

	// A gap, then the cursor placed so a row of buttons with these labels is centred.
	void BeginButtonRow(std::initializer_list<const char*> a_labels)
	{
		constexpr float kGap = 17.0f;  // pixels at 1080p, on top of one extra item spacing

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y + kGap * Theme::Scale());
		CentreButtonRow(a_labels);
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

	// The outfit pages' `a_count` equal columns; every table of them on a page lines up, each
	// with its own `a_id`. When it returns true, fill the cells and call EndColumns with the
	// same count, which draws the rules between them (Theme::ColumnRule).
	bool BeginColumns(const char* a_id, int a_count)
	{
		constexpr float kColumnGap = 36.0f;  // pixels at 1080p, between the columns

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(kColumnGap * 0.5f * Theme::Scale(), ImGui::GetStyle().CellPadding.y));
		// NoClip lets the lists reach the window's edges (Theme::BeginList).
		if (!ImGui::BeginTable(a_id, a_count, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoClip)) {
			ImGui::PopStyleVar();
			return false;
		}
		for (int column = 0; column < a_count; ++column) {
			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
		}
		return true;
	}

	void EndColumns(int a_count)
	{
		ImGui::EndTable();
		ImGui::PopStyleVar();
		// The table is the last item: its rect spans every column, which are equally wide.
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		for (int rule = 1; rule < a_count; ++rule) {
			Theme::ColumnRule(min.x + (max.x - min.x) * static_cast<float>(rule) / static_cast<float>(a_count), min.y, max.y);
		}
	}

	void RequestCancel()
	{
		SKSE::GetTaskInterface()->AddTask([] { CaptureWindow::Close(); });
	}

	// The Dress page, the Wardrobe page, the Situations page, the Chests page and Appearance
	// have the tab column at the window's left edge; the pages they lead to do not.
	bool HasTabs(Page a_page)
	{
		return a_page == Page::kCapture || a_page == Page::kManage || a_page == Page::kSituations || a_page == Page::kChests || a_page == Page::kAppearance;
	}

	// The Dress page, the Wardrobe page, the Situations page, the Chests page (Request::offersChests),
	// then Appearance, each centred between the window's left edge and the rule. A click on
	// another tab leaves the page for it (RequestLeave).
	void DrawTabColumn(State& a_state)
	{
		struct TabOption
		{
			Theme::TabIcon icon;
			const char*    name;
			Page           page;
		};
		constexpr std::array<TabOption, 5> kTabs{ {
			{ Theme::TabIcon::kDress, "Dress", Page::kCapture },
			{ Theme::TabIcon::kWardrobe, "Wardrobe", Page::kManage },
			{ Theme::TabIcon::kSituations, "Situations", Page::kSituations },
			{ Theme::TabIcon::kChests, "Chests", Page::kChests },
			{ Theme::TabIcon::kAppearance, "Appearance", Page::kAppearance },
		} };

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float  left = origin.x + Theme::TabOffsetX();
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		ImGui::SetCursorScreenPos(ImVec2(left, origin.y + Theme::TabTopGap()));
		for (const auto& tab : kTabs) {
			if (a_state.request.saveOnly && (tab.page == Page::kManage || tab.page == Page::kSituations)) {
				continue;
			}
			if (tab.page == Page::kChests && !a_state.request.offersChests) {
				continue;
			}
			const bool selected = a_state.page == tab.page;
			if (Theme::Tab(tab.icon, tab.name, selected) && !selected) {
				QueueMenuSound(kFocusSound);
				RequestLeave(a_state, { .page = tab.page });
			}
			ImGui::SetCursorScreenPos(ImVec2(left, ImGui::GetCursorScreenPos().y));  // a new line starts at the content's left edge
		}
		ImGui::EndDisabled();
	}

	void RequestLeave(State& a_state, PendingLeave a_leave)
	{
		const bool edits = (a_state.page == Page::kManage && HasManageEdits(a_state)) || (a_state.page == Page::kSituations && HasSituationEdits(a_state)) ||
		                   (a_state.page == Page::kChestEdit && HasChestEdits(a_state));
		if (edits) {
			a_state.leave = a_leave;
			a_state.openLeave = true;  // the page opens the alert in its own ID scope
			return;
		}
		if (a_leave.exit) {
			QueueMenuSound(kCancelSound);
		}
		Leave(a_state, a_leave);
	}

	// The pieces a tab page keeps on the preview in place of the Dress page's ticks:
	// the Wardrobe page's selected outfit as edited, less its unticked items, the Situations page's
	// outfit whose details show (ViewedOutfitPieces); nullopt while it shows the ticks. A page
	// that gets a preview of its own adds it here.
	std::optional<std::vector<std::size_t>> PagePreview(const State& a_state, Page a_page)
	{
		if (a_page == Page::kManage && a_state.manage.outfit >= 0) {
			return WornOf(a_state, a_state.manage.pieces, a_state.manage.excluded);
		}
		if (a_page == Page::kSituations && IndexOfOutfit(a_state, a_state.situationForm.viewedOutfit) >= 0) {
			return ViewedOutfitPieces(a_state);
		}
		return std::nullopt;
	}

	// Every heading of the Situations page is open when its tab opens, and an outfit's details
	// there close as it goes. The preview shows the opened page's own pieces (PagePreview), or
	// the ticks again when only the page left had its own.
	void Leave(State& a_state, const PendingLeave& a_leave)
	{
		if (a_leave.exit) {
			RequestCancel();
			return;
		}
		const bool ownPreview = PagePreview(a_state, a_state.page).has_value();
		a_state.situationForm.viewedOutfit = 0;
		a_state.situationForm.viewedRow = -1;
		const int target = a_leave.page == Page::kManage ? IndexOfOutfit(a_state, a_leave.manageOutfit) : -1;
		if (target >= 0) {
			// Contract: the Wardrobe page has no unsaved edits now: leaving it with any went through
			// the unsaved-changes alert, which saved or dropped them.
			FillManageForm(a_state, target);
		}
		if (auto pieces = PagePreview(a_state, a_leave.page)) {
			QueuePreview(a_state, std::move(*pieces));
		} else if (ownPreview) {
			QueuePreview(a_state, TickedPieces(a_state));
		}
		if (a_leave.page == Page::kSituations) {
			for (auto& entry : a_state.situationForm.entries) {
				entry.folded = false;
			}
		}
		GoToPage(a_state, a_leave.page);
	}

	LeaveChoice DrawLeavePopup(State& a_state, const std::string& a_text, const char* a_noSave, void (*a_drawDetails)(const State&))
	{
		if (!BeginModal(kLeavePopup)) {
			return LeaveChoice::kNone;
		}
		if (!a_state.leave) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return LeaveChoice::kNone;
		}
		ImGui::TextUnformatted(a_text.c_str());
		if (a_drawDetails) {
			a_drawDetails(a_state);
		}
		BeginButtonRow({ "Save Changes", "Discard Changes", "Back" });
		const bool save = ReasonButton("Save Changes", a_noSave);
		ImGui::SameLine();
		const bool discard = Theme::Button("Discard Changes");
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		auto choice = LeaveChoice::kNone;
		if (save) {
			choice = LeaveChoice::kSave;
		} else if (discard) {
			choice = LeaveChoice::kDiscard;
		} else if (back) {
			choice = LeaveChoice::kBack;
			a_state.leave.reset();
		}
		if (choice != LeaveChoice::kNone) {
			QueueMenuSound(choice == LeaveChoice::kBack ? kCancelSound : kOkSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return choice;
	}

	// The alert's Save Changes: once the save is over, the leave follows when it took; a
	// refused save stays, with its error.
	void FinishLeave(State& a_state)
	{
		if (!a_state.leaveAfterSave || a_state.status != Status::kEditing) {
			return;
		}
		a_state.leaveAfterSave = false;
		const auto leave = std::exchange(a_state.leave, std::nullopt);
		if (leave && a_state.error.empty()) {
			Leave(a_state, *leave);
		}
	}

	// `a_title` over the actor's summary line.
	void DrawSummaryHeading(const char* a_title, const CaptureWindow::ActorSummary& a_summary)
	{
		if (a_summary.outfit.empty()) {
			Theme::HeadingText(a_title, a_summary.lead.c_str());
			return;
		}
		const std::array<Theme::SubtitleRun, 5> runs{ {
			{ .text = a_summary.lead.c_str() },
			{ .text = a_summary.outfit.c_str(), .rowIcon = RowIconOf(a_summary.icon) },
			{ .text = a_summary.link.c_str() },
			{ .text = a_summary.situation.c_str(), .icon = a_summary.situationIsDefault ? Theme::HeadingIcon::kBareForm : Theme::HeadingIcon::kStar },
			{ .text = a_summary.tail.c_str() },
		} };
		Theme::HeadingText(a_title, runs);
	}

	// The page's heading: the Dress page's own title over the summary line; Manage
	// Wardrobe's, the Situations page's and the Edit page's with no summary line; the Load page's the actor's name over the summary line;
	// Appearance's over where the other settings are. The Save page's tabs are its heading (DrawSaveTabs).
	void DrawTitle(const State& a_state)
	{
		switch (a_state.page) {
		case Page::kCapture:
			DrawSummaryHeading(a_state.captureTitle.c_str(), a_state.request.actorSummary);
			break;
		case Page::kManage:
			Theme::HeadingText("Wardrobe");
			break;
		case Page::kSituations:
			Theme::HeadingText(a_state.situationsTitle.c_str());
			break;
		case Page::kEdit:
			Theme::HeadingText(a_state.editTitle.c_str());
			break;
		case Page::kChests:
			{
				const std::array<Theme::SubtitleRun, 3> subtitle{ {
					{ .text = "Items in a chest can be loaded on the " },
					{ .text = "Dress", .icon = Theme::HeadingIcon::kBareForm },
					{ .text = " page." },
				} };
				Theme::HeadingText("Chests", subtitle);
				break;
			}
		case Page::kChestEdit:
			Theme::HeadingText(a_state.chest.chest >= 0 ? std::format("Edit \"{}\"", ChestAt(a_state, a_state.chest.chest).name).c_str() : "Edit");
			break;
		case Page::kLoad:
			DrawSummaryHeading(a_state.request.actorName.c_str(), a_state.request.actorSummary);
			break;
		case Page::kAppearance:
			Theme::HeadingText("Appearance", "Other settings are in Livery.ini or on the SKSE Menu Framework page.");
			break;
		case Page::kSave:
			break;
		}
	}

	// Input over the game goes to onViewInput, in one SKSE task per frame that
	// has any, and in one each frame while the preview camera glides
	// (FreeCamera::Gliding), empty when there is none. A drag is a button held
	// since a press the window did not own; the mouse moves of the frame it was
	// held (collected by QueueInput) are a look for the left button and a pan
	// for the right. Keys count while no item is active and no popup is open
	// (WantCaptureKeyboard), the wheel while the cursor is over the game. None
	// while Live Preview is off: a drag is dropped, so QueueInput moves the
	// cursor again.
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
		if (any || FreeCamera::Gliding()) {
			SKSE::GetTaskInterface()->AddTask([onViewInput, input] { onViewInput(input); });
		}
	}

	// A help text in a tooltip, wrapped to a readable width.
	void DrawHelpTooltip(const std::string& a_text)
	{
		constexpr float kWrapEms = 28.0f;  // the tooltip's text width, in font sizes

		Theme::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * kWrapEms);
		ImGui::TextUnformatted(a_text.c_str());
		ImGui::PopTextWrapPos();
		Theme::EndTooltip();
	}

	// Opens a modal popup centred on the screen, in the skin; when it returns true, fill it
	// and call ImGui::EndPopup.
	// Contract: called every frame on the page that opens it: a modal that is open but not
	// submitted stays invisible and still blocks the window.
	bool BeginModal(const char* a_id)
	{
		const auto& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		constexpr auto flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
		if (!ImGui::BeginPopupModal(a_id, nullptr, flags)) {
			return false;
		}
		Theme::DecorateWindow(true);
		return true;
	}

	// --- Capture page ---------------------------------------------------------

	bool FadesNotCarried(const State& a_state, std::size_t a_piece)
	{
		return a_state.request.carriedOnly && a_state.request.pieces[a_piece].group == CaptureWindow::PieceGroup::kNotCarried;
	}

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
		auto&       headings = a_state.headings;
		headings[GroupIndex(Group::kLiveryOutfit)] = std::format("Outfit (\"{}\")", request.outfitName);
		headings[GroupIndex(Group::kNotInLibrary)] = "Outfit (Not in Wardrobe)";
		headings[GroupIndex(Group::kUnmanaged)] = "Outfit (Unmanaged)";
		headings[GroupIndex(Group::kCarried)] = "Inventory";
		// Theme::ListHeadingHelp adds its "(?)".
		headings[GroupIndex(Group::kPlayer)] = std::format("{}'s Inventory", request.playerName.empty() ? "Player" : request.playerName);
		headings[GroupIndex(Group::kNotCarried)].clear();
	}

	// The column's order of two pieces: negative, zero or positive.
	int CompareColumn(const CaptureWindow::Piece& a_left, const CaptureWindow::Piece& a_right, int a_column)
	{
		switch (a_column) {
		case kTypeColumn:
			return a_left.typeOrder - a_right.typeOrder;
		case kSlotColumn:
			return a_left.slotOrder - a_right.slotOrder;
		default:
			return 0;  // the name decides below
		}
	}

	// Orders the pieces for the list: by group, as the headings go, then by the sorted
	// column (Type in SkyUI's order, Slot by the lowest slot), the name breaking ties, all of it reversed
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

	// SetOutfits after the captured actor was redressed with the window open:
	// the Dress page lists their pieces as they are now, ticks the ones they wear and lets
	// the loaded outfit go, as if the window had just opened.
	// Contract: `a_pieces.groups` has one group per request.pieces.
	void FollowActorPieces(State& a_state, const CaptureWindow::ActorPieces& a_pieces)
	{
		auto& request = a_state.request;
		for (std::size_t i = 0; i < request.pieces.size(); ++i) {
			request.pieces[i].group = a_pieces.groups[i];
			a_state.pieceTicked[i] = false;
		}
		for (const auto index : a_pieces.ticked) {
			a_state.pieceTicked[index] = true;
		}
		request.outfitName = a_pieces.outfitName;
		request.wornOutfit = a_pieces.wornOutfit;
		request.wornSituation = a_pieces.wornSituation;
		request.wornFor = a_pieces.wornFor;
		a_state.loadedOutfit = -1;
		a_state.inventorySources.clear();
		SetInventoryShown(a_state);
		SortPieces(a_state);
	}

	// After SetOutfits: what the Dress page has loaded follows the library and the chests.
	// A whole one brings its pieces as they are now, one a later Load narrowed those it still
	// has; one that is gone or brings nothing leaves, its name with it.
	void FollowInventorySources(State& a_state)
	{
		auto& sources = a_state.inventorySources;
		for (auto& source : sources) {
			const int index = source.chest ? IndexOfChest(a_state, source.id) : IndexOfOutfit(a_state, source.id);
			if (index < 0) {
				source.pieces.clear();
				continue;
			}
			const auto& pieces = source.chest ? ChestAt(a_state, index).pieces : OutfitAt(a_state, index).pieces;
			source.name = source.chest ? ChestAt(a_state, index).name : OutfitAt(a_state, index).name;
			if (source.whole) {
				source.pieces = pieces;
			} else {
				std::erase_if(source.pieces, [&pieces](std::size_t a_piece) { return !std::ranges::binary_search(pieces, a_piece); });
			}
		}
		std::erase_if(sources, [](const ShownSource& a_source) { return a_source.pieces.empty(); });
		SetInventoryShown(a_state);
	}

	// After SetOutfits: the pieces Livery put on from Request::wornOutfit keep their heading
	// under the outfit's name now. When it has left the library they move under the
	// Not in Wardrobe heading, and Apply no longer offers to update it.
	void FollowWornOutfit(State& a_state)
	{
		auto& request = a_state.request;
		if (request.wornOutfit == 0) {
			return;
		}
		if (const int index = IndexOfOutfit(a_state, request.wornOutfit); index >= 0) {
			request.outfitName = OutfitAt(a_state, index).name;
			return;
		}
		request.wornOutfit = 0;
		request.wornSituation.clear();
		request.wornFor = Situations::Situation::kDefault;
		request.outfitName.clear();
		for (auto& piece : request.pieces) {
			if (piece.group == CaptureWindow::PieceGroup::kLiveryOutfit) {
				piece.group = CaptureWindow::PieceGroup::kNotInLibrary;
			}
		}
		SortPieces(a_state);
	}

	// Name takes what Type and Slot leave at the list's right end, each of them as wide
	// as its widest text, or its label with room for the sort arrow; Slot no wider than four
	// slots, a longer list ending in an ellipsis. Without Slot (State::showSlots), Type ends at
	// the right end. Name starts past the pieces' icons.
	std::array<float, 3> ColumnPositions(const State& a_state)
	{
		constexpr const char* kWidestSlots = "00, 00, 00, 00";  // the Slot column's widest

		const float labelRoom = ImGui::GetFontSize();  // the small-font label's arrow and its gap fit in a body-font em
		const float slotLabel = ImGui::CalcTextSize(kColumnLabels[kSlotColumn]).x + labelRoom;
		float       typeWidth = ImGui::CalcTextSize(kColumnLabels[kTypeColumn]).x + labelRoom;
		float       slotWidth = slotLabel;
		for (const auto& piece : a_state.request.pieces) {
			typeWidth = (std::max)(typeWidth, ImGui::CalcTextSize(piece.type.c_str()).x);
			slotWidth = (std::max)(slotWidth, ImGui::CalcTextSize(piece.slotText.c_str()).x);
		}
		slotWidth = (std::min)(slotWidth, (std::max)(slotLabel, ImGui::CalcTextSize(kWidestSlots).x));
		const float slotX = (std::max)(0.0f, Theme::ColumnsWidth() - slotWidth);
		const float nameX = Theme::RowIconRoom();
		const float typeRight = a_state.showSlots ? slotX - Theme::ColumnGap() : Theme::ColumnsWidth();
		const float typeX = (std::max)(nameX, typeRight - typeWidth);
		return { nameX, typeX, slotX };
	}

	// Ticking a piece unticks the ticked ones sharing a body slot with it, as
	// equipping it takes them off; their onPieceToggled tasks go first.
	void UntickSharingSlots(State& a_state, const PieceList& a_list, std::size_t a_index)
	{
		const auto& pieces = a_state.request.pieces;
		const auto& onToggled = a_state.request.onPieceToggled;
		for (std::size_t j = 0; j < pieces.size(); ++j) {
			if (j == a_index || !a_list.ticked[j] || (pieces[j].slots & pieces[a_index].slots) == 0) {
				continue;
			}
			a_list.ticked[j] = false;
			if (onToggled) {
				SKSE::GetTaskInterface()->AddTask([onToggled, j] { onToggled(j, false); });
			}
		}
	}

	// The groups that have a listed piece, so their headings show.
	std::size_t CountListedGroups(const State& a_state, const PieceList& a_list)
	{
		std::array<bool, kPieceGroupCount> listed{};
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			if (IsListed(a_state, a_list, i)) {
				listed[GroupIndex(a_state.request.pieces[i].group)] = true;
			}
		}
		return static_cast<std::size_t>(std::ranges::count(listed, true));
	}

	// A group's icon: a backpack over carried pieces, a dressed form over worn
	// ones, a bare form over a loaded outfit's.
	Theme::HeadingIcon GroupIcon(CaptureWindow::PieceGroup a_group)
	{
		using Group = CaptureWindow::PieceGroup;
		switch (a_group) {
		case Group::kCarried:
		case Group::kPlayer:
			return Theme::HeadingIcon::kBackpack;
		case Group::kNotCarried:
			return Theme::HeadingIcon::kBareForm;
		default:
			return Theme::HeadingIcon::kDressedForm;
		}
	}

	// A group's heading inside the list: greyed, in the label font, left of the names, its icon
	// first (GroupIcon). The player's group explains on hover what Apply does with its pieces
	// and what it leaves out.
	void DrawGroupHeading(const State& a_state, const PieceList& a_list, CaptureWindow::PieceGroup a_group)
	{
		const bool  notCarried = a_group == CaptureWindow::PieceGroup::kNotCarried;
		const auto& heading = notCarried ? *a_list.heading : a_state.headings[GroupIndex(a_group)];
		const auto  icon = notCarried ? a_list.shownIcon : GroupIcon(a_group);
		if (a_group != CaptureWindow::PieceGroup::kPlayer) {
			Theme::ListHeading(heading.c_str(), icon);
			return;
		}
		if (!Theme::ListHeadingHelp(heading.c_str(), icon)) {
			return;
		}
		const auto& name = a_state.request.actorName;
		const auto  apply = a_state.request.movesFromPlayer ? std::format("\"Apply\" moves the ticked items from your inventory to {}.", name) :
		                                                      std::format("\"Apply\" gives {} a new copy of each ticked item; yours stay with you.", name);
		DrawHelpTooltip(apply + (a_state.request.protectsFavorites ? " Quest items and your favorites are not listed." : " Quest items are not listed."));
	}

	// Height of the piece list with every listed piece and heading in view, capped as
	// ListHeight caps it. Contract: called with the list's row spacing pushed.
	float PieceListHeight(const State& a_state, const PieceList& a_list)
	{
		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		const float rowsHeight = static_cast<float>(CountListedPieces(a_state, a_list)) * (ImGui::GetFrameHeight() + spacing) +
		                         static_cast<float>(CountListedGroups(a_state, a_list)) * (Theme::ListHeadingHeight() + spacing);
		return FittedListHeight(rowsHeight, ImGui::GetFrameHeightWithSpacing());
	}

	// The column labels, then the listed pieces under their groups' headings, a tick row
	// each; unticked pieces are left out of the outfit. A click on a label sorts by its
	// column, a second one reverses the order (both pages share the order). Each tick goes
	// to onPieceToggled (the live preview) in its own SKSE task, queued in click order.
	// Contract: a piece that is not listed is never ticked: on the Dress page only Load
	// ticks a piece nobody carries, and the next Load sets every tick again; the Edit page
	// lists every edited piece it opened with.
	void DrawPieceRows(State& a_state, const PieceList& a_list)
	{
		const auto&       pieces = a_state.request.pieces;
		const auto&       onToggled = a_state.request.onPieceToggled;
		const bool        locked = a_state.request.saveOnly;
		const auto        positions = ColumnPositions(a_state);
		const std::size_t columns = a_state.showSlots ? kColumnLabels.size() : static_cast<std::size_t>(kSlotColumn);  // Slot is the last
		const auto        labels = std::span(kColumnLabels).first(columns);
		const auto        columnX = std::span<const float>(positions).first(columns);
		if (const int clicked = Theme::HeaderRow(labels, columnX, a_state.sortColumn, a_state.sortDescending); clicked >= 0) {
			QueueMenuSound(kFocusSound);
			a_state.sortDescending = clicked == a_state.sortColumn && !a_state.sortDescending;
			a_state.sortColumn = clicked;
			SortPieces(a_state);
		}
		PushListRowSpacing();  // popped before EndList, so the gap below the list is the normal one
		if (Theme::BeginList("pieces", PieceListHeight(a_state, a_list))) {
			std::size_t group = kPieceGroupCount;  // none drawn yet
			for (const auto i : a_state.pieceOrder) {
				if (!IsListed(a_state, a_list, i)) {
					continue;
				}
				if (GroupIndex(pieces[i].group) != group) {
					group = GroupIndex(pieces[i].group);
					DrawGroupHeading(a_state, a_list, pieces[i].group);
				}
				ImGui::PushID(static_cast<int>(i));
				const std::array<const char*, 3> cells{ pieces[i].name.c_str(), pieces[i].type.c_str(), pieces[i].slotText.c_str() };
				// Save only: the row takes no click and shows no hover, yet does not grey (DisabledAlpha 1).
				ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, locked ? 1.0f : ImGui::GetStyle().DisabledAlpha);
				ImGui::BeginDisabled(locked);
				// Times the current alpha: a page greyed by BeginDisabled stays greyer still.
				const bool faded = FadesNotCarried(a_state, i);
				if (faded) {
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kNotCarriedAlpha);
				}
				const bool toggled = Theme::TickRow(std::span(cells).first(columns), columnX, &a_list.ticked[i], { pieces[i].icon, pieces[i].iconColor });
				if (faded) {
					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered()) {
						Theme::Tooltip(kNotCarriedHint);
					}
				}
				ImGui::EndDisabled();
				ImGui::PopStyleVar();
				if (toggled) {
					QueueMenuSound(kFocusSound);
					if (a_list.ticked[i]) {
						UntickSharingSlots(a_state, a_list, i);
					}
					if (onToggled) {
						SKSE::GetTaskInterface()->AddTask([onToggled, i, ticked = a_list.ticked[i]] { onToggled(i, ticked); });
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
	}

	// A link of the count line, greyed with `a_reason` (shown on hover). Returns true when clicked.
	bool DrawCountLink(const char* a_name, const char* a_reason, Theme::HeadingIcon a_icon, Theme::LinkLine a_line, float a_reserveRight)
	{
		ImGui::BeginDisabled(a_reason != nullptr);
		const bool clicked = Theme::LabelLink(kAddFrom, a_line, a_reserveRight, a_icon, a_name);
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		// After EndDisabled, so the tooltip is not drawn faded.
		if (a_reason && hovered) {
			Theme::Tooltip(a_reason);
		}
		return clicked;
	}

	// The count line under the list; on the Dress and Edit pages (`a_withLinks`) it ends in
	// `Add from Wardrobe...`, which opens the Load page and is greyed while the library is
	// empty, with Request::offersChests `Add from Chest...` on the line below it, greyed while
	// there is no chest. Returns which link was clicked.
	CountLinks DrawPieceCount(const State& a_state, const PieceList& a_list, bool a_withLinks)
	{
		const auto ticked = Ticked(a_state, a_list).size();
		Theme::CountText(std::format("{} of {} item(s) selected", ticked, CountListedPieces(a_state, a_list)).c_str(), false);
		CountLinks links;
		if (a_withLinks) {
			constexpr float kLinkRowGap = 4.0f;  // pixels at 1080p, between the two links, on top of a next line's own gap

			links.wardrobe = DrawCountLink("Wardrobe...", a_state.request.existingOutfits.empty() ? kNoOutfits : nullptr, Theme::HeadingIcon::kWardrobe);
			if (a_state.request.offersChests) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kLinkRowGap * Theme::Scale());
				links.chest = DrawCountLink("Chest...", a_state.request.chests.empty() ? kNoChests : nullptr, Theme::HeadingIcon::kChest, Theme::LinkLine::kNext);
			}
		}
		if (ticked == 0) {
			ImGui::TextDisabled(kNeedPiece);
		}
		return links;
	}

	// Restore and its "(?)" at the right end of the button row, while Livery dresses
	// the character (Request::dressed, Request::onRestore). Returns true when it is clicked.
	// Contract: right after the row's last button, on its line.
	bool DrawRestoreLink(const State& a_state)
	{
		if (!a_state.request.onRestore || !a_state.request.dressed) {
			return false;
		}
		bool       hovered = false;
		const bool restore = Theme::LineHelpLink("Restore", &hovered);
		if (hovered) {
			DrawHelpTooltip(std::format(
				"Takes Livery's outfit off {} and forgets their situational outfits. Items moved from your inventory stay with them.",
				a_state.request.actorName));
		}
		return restore;
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
		constexpr const char*                kSummary = "Keeps the game running and dresses the character in the ticked items.";
		constexpr std::array<const char*, 5> kControls{
			"With the cursor over the game:",
			"Left drag - turns around the character",
			"Right drag - pans",
			"Mouse wheel - zooms",
			"W / A / S / D - moves (hold Shift for half speed)",
		};
		constexpr float kLineGap = 2.0f;     // pixels at 1080p, between lines
		constexpr float kSummaryGap = 8.0f;  // pixels at 1080p, between the summary and the controls

		Theme::BeginTooltip();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kLineGap * Theme::Scale()));
		ImGui::TextUnformatted(kSummary);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (kSummaryGap - kLineGap) * Theme::Scale());
		for (const char* line : kControls) {
			ImGui::TextUnformatted(line);
		}
		ImGui::PopStyleVar();
		Theme::EndTooltip();
	}

	// The Live Preview tick: greyed, with the reason on hover, when the preview
	// cannot be on; each click queues ApplyPreviewToggle with the list's ticked
	// pieces of that moment, so the piece clicks queued after it apply on top. A
	// "(?)" beside it explains the preview and its camera controls on hover.
	void DrawPreviewToggle(State& a_state, const PieceList& a_list)
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
			Theme::Tooltip(note.c_str());
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
				[session = a_state.session, on, pieces = Ticked(a_state, a_list)] { ApplyPreviewToggle(session, on, pieces); });
		}
	}

	// The library outfit Livery has on the actor: index into request.existingOutfits, -1 for none.
	int WornOutfitIndex(const State& a_state)
	{
		const auto& outfits = a_state.request.existingOutfits;
		const auto  found = std::ranges::find(outfits, a_state.request.wornOutfit, &CaptureWindow::ExistingOutfit::id);
		return a_state.request.wornOutfit != 0 && found != outfits.end() ? static_cast<int>(found - outfits.begin()) : -1;
	}

	// The id of the outfit last loaded when the ticks are exactly its pieces; 0 otherwise.
	std::uint32_t TickedOutfitId(const State& a_state)
	{
		const int loaded = a_state.loadedOutfit;
		return loaded >= 0 && TickedPieces(a_state) == OutfitAt(a_state, loaded).pieces ? OutfitAt(a_state, loaded).id : 0;
	}

	// When the ticks are all or some of the pieces of `a_worn` (WornOutfitIndex) and nothing
	// else: the armor (FormIDs) of its pieces left unticked, empty for none. nullopt otherwise.
	std::optional<std::vector<std::uint32_t>> UntickedOfWorn(const State& a_state, int a_worn)
	{
		const auto& pieces = OutfitAt(a_state, a_worn).pieces;
		const auto  ticked = TickedPieces(a_state);
		const auto  inOutfit = [&pieces](std::size_t a_piece) { return std::ranges::binary_search(pieces, a_piece); };
		if (!std::ranges::all_of(ticked, inOutfit)) {
			return std::nullopt;
		}
		std::vector<std::uint32_t> unticked;
		for (const auto piece : pieces) {
			if (!std::ranges::binary_search(ticked, piece)) {
				unticked.push_back(a_state.request.pieces[piece].armor);
			}
		}
		return unticked;
	}

	// The widest button row of the Capture page, which sets the page's least width: the row
	// is centred, so Restore at the line's right end needs its width and an item
	// spacing on either side of it, whatever the font or screen.
	float CaptureButtonsWidth()
	{
		const float restore = Theme::LineHelpLinkWidth("Restore") + ImGui::GetStyle().ItemSpacing.x;
		return ButtonRowWidth({ "Create / Update Outfit", "Apply", "Exit" }) + restore * 2.0f;
	}

	enum class CaptureAction
	{
		kNone,
		kLoadPage,
		kSavePage,
		kApply,
		kRestore,
		kExit
	};

	// Create / Update Outfit, Apply and Exit centred below a gap, then Restore at the
	// row's right end. Create / Update Outfit and Apply act on the ticked pieces, so they need at least one. `a_load`: Add
	// from Wardrobe... or Add from Chest... was clicked under the list.
	CaptureAction DrawCaptureButtons(const State& a_state, bool a_load)
	{
		constexpr const char* kSaveTooltip = "Save the ticked items as a new outfit, or into an existing one.";

		const bool saveOnly = a_state.request.saveOnly;
		if (saveOnly) {
			BeginButtonRow({ "Create / Update Outfit", "Exit" });
		} else {
			BeginButtonRow({ "Create / Update Outfit", "Apply", "Exit" });
		}
		const char* noPiece = CountTickedPieces(a_state) == 0 ? kNeedPiece : nullptr;
		const bool  save = ReasonButton("Create / Update Outfit", noPiece, kSaveTooltip);
		ImGui::SameLine();
		bool apply = false;
		if (!saveOnly) {
			apply = ReasonButton("Apply", noPiece, a_state.applyTooltip.c_str());
			ImGui::SameLine();
		}
		// While the Apply or Restore popup is open, Escape is its own Cancel.
		const bool exitPressed = Theme::Button("Exit") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
		const bool restore = DrawRestoreLink(a_state);

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Applying...");
			return CaptureAction::kNone;
		}
		if (a_load) {
			return CaptureAction::kLoadPage;
		}
		if (save) {
			return CaptureAction::kSavePage;
		}
		if (apply) {
			return CaptureAction::kApply;
		}
		if (restore) {
			return CaptureAction::kRestore;
		}
		return exitPressed ? CaptureAction::kExit : CaptureAction::kNone;
	}

	// The ticked pieces under Player's Inventory, as the list shows them now: those Apply moves.
	std::vector<std::size_t> TickedFromPlayer(const State& a_state)
	{
		auto pieces = TickedPieces(a_state);
		std::erase_if(pieces, [&a_state](std::size_t a_piece) { return a_state.request.pieces[a_piece].group != CaptureWindow::PieceGroup::kPlayer; });
		return pieces;
	}

	// `a_updateWorn`: Yes in the Apply popup, so `a_outfit` is the worn library outfit, which
	// the handler updates with the ticks before it dresses the actor. The ticked pieces under
	// Player's Inventory, as the list shows them now, are those that move. `a_excluded`: the
	// armor of the worn outfit `a_outfit` left unticked (UntickedOfWorn).
	void RequestApply(State& a_state, std::uint32_t a_outfit, bool a_updateWorn, std::vector<std::uint32_t> a_excluded = {})
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto pieces = TickedPieces(a_state);
		auto fromPlayer = TickedFromPlayer(a_state);
		auto onApply = a_state.request.onApply;
		SKSE::GetTaskInterface()->AddTask(
			[onApply, pieces = std::move(pieces), fromPlayer = std::move(fromPlayer), a_outfit, a_updateWorn, excluded = std::move(a_excluded)] {
				onApply(pieces, fromPlayer, a_outfit, a_updateWorn, excluded);
			});
	}

	// Apply: when Livery has a library outfit on the actor, the ticks all or some of its pieces
	// and nothing else go on at once as that outfit, the unticked ones left off (as Manage
	// Wardrobe's Apply leaves them); other ticks ask first in the popup. Else the ticks go on
	// at once.
	// Contract: called in the window's own ID scope, as DrawApplyPopup is.
	void BeginApply(State& a_state)
	{
		const int worn = WornOutfitIndex(a_state);
		if (worn < 0) {
			RequestApply(a_state, TickedOutfitId(a_state), false);
			return;
		}
		if (auto unticked = UntickedOfWorn(a_state, worn)) {
			RequestApply(a_state, a_state.request.wornOutfit, false, std::move(*unticked));
			return;
		}
		ImGui::OpenPopup(kApplyPopup);
	}

	enum class ApplyChoice
	{
		kNone,
		kUpdate,
		kJustApply,
		kCancel
	};

	// Whether a situation row of the actor holds an outfit (rows after the Default).
	bool HasSituationalOutfits(const State& a_state)
	{
		const auto& rows = a_state.request.assigned;
		return std::any_of(rows.begin() + (rows.empty() ? 0 : 1), rows.end(), [](const CaptureWindow::RowAssignment& a_row) { return OutfitRows::HoldsAny(a_row.row); });
	}

	// `Update "X" with the ticked pieces?`, naming the situation X is worn for, then Yes,
	// No, apply as is (its "(?)" inside the button; the help speaks of situational outfits
	// only when the actor has one) or Cancel.
	ApplyChoice DrawApplyPopup(const State& a_state)
	{
		constexpr const char* kJustApply = "No, apply as is";
		constexpr const char* kJustApplyWidth = "No, apply as is (?)";  // what the button's width is measured on (Theme::HelpButton)

		if (!BeginModal(kApplyPopup)) {
			return ApplyChoice::kNone;
		}
		const int worn = WornOutfitIndex(a_state);
		if (worn < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return ApplyChoice::kNone;
		}
		const auto& request = a_state.request;
		const auto& outfit = OutfitAt(a_state, worn);
		ImGui::TextUnformatted(std::format("Update \"{}\" with the ticked items?", outfit.name).c_str());
		if (!request.wornSituation.empty()) {
			ImGui::TextUnformatted(std::format("\"{}\" is {}'s {} outfit.", outfit.name, request.actorName, request.wornSituation).c_str());
		}
		BeginButtonRow({ "Yes", kJustApplyWidth, "Cancel" });
		const bool update = Theme::Button("Yes");
		ImGui::SameLine();
		bool       helpHovered = false;
		const bool justApply = Theme::HelpButton(kJustApply, &helpHovered);
		if (helpHovered) {
			auto help = std::format("The ticked items become {}'s Default as they are, without being added to the wardrobe.", request.actorName);
			if (HasSituationalOutfits(a_state)) {
				help += " While a situational outfit is worn, the new Default shows only when that situation ends.";
			}
			DrawHelpTooltip(help);
		}
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		auto choice = ApplyChoice::kNone;
		if (update) {
			choice = ApplyChoice::kUpdate;
		} else if (justApply) {
			choice = ApplyChoice::kJustApply;
		} else if (cancel) {
			choice = ApplyChoice::kCancel;
		}
		if (choice != ApplyChoice::kNone) {
			QueueMenuSound(choice == ApplyChoice::kCancel ? kCancelSound : kOkSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return choice;
	}

	void RequestRestore(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onRestore = a_state.request.onRestore;
		SKSE::GetTaskInterface()->AddTask([onRestore] { onRestore(); });
	}

	// `Restore #{Name}'s original outfit?`, what it forgets, then Restore / Cancel. Returns
	// true on Restore.
	bool DrawRestorePopup(const State& a_state)
	{
		if (!BeginModal(kRestorePopup)) {
			return false;
		}
		const auto& name = a_state.request.actorName;
		ImGui::TextUnformatted(std::format("Restore {}'s original outfit?", name).c_str());
		ImGui::TextUnformatted(std::format("This takes Livery's outfit off {} and forgets every situational outfit chosen for them.", name).c_str());
		BeginButtonRow({ "Restore", "Cancel" });
		const bool restore = Theme::Button("Restore");
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (restore || cancel) {
			QueueMenuSound(restore ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return restore;
	}

	void DrawCapturePage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		const auto list = InventoryPieces(a_state);
		DrawPieceRows(a_state, list);
		const auto links = DrawPieceCount(a_state, list, a_state.request.offersWardrobe);
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		DrawPreviewToggle(a_state, list);
		const auto action = DrawCaptureButtons(a_state, links.wardrobe || links.chest);
		EndButtonRow();
		ImGui::EndDisabled();

		switch (DrawApplyPopup(a_state)) {
		case ApplyChoice::kUpdate:
			RequestApply(a_state, a_state.request.wornOutfit, true);
			break;
		case ApplyChoice::kJustApply:
			RequestApply(a_state, TickedOutfitId(a_state), false);
			break;
		case ApplyChoice::kCancel:
		case ApplyChoice::kNone:
			break;
		}
		if (DrawRestorePopup(a_state)) {
			RequestRestore(a_state);
		}

		switch (action) {
		case CaptureAction::kLoadPage:
			QueueMenuSound(kOkSound);
			a_state.previewedOutfit = -1;
			a_state.previewedChest = -1;
			a_state.loadTarget = LoadTarget::kInventory;
			a_state.loadSource = links.chest ? LoadSource::kChests : LoadSource::kOutfits;
			GoToPage(a_state, Page::kLoad);
			break;
		case CaptureAction::kSavePage:
			QueueMenuSound(kOkSound);
			if (a_state.updateTab) {
				PreviewOutfitOrTicks(a_state, a_state.chosenOutfit);  // the tab shows its outfit again
			}
			GoToPage(a_state, Page::kSave);
			break;
		case CaptureAction::kApply:
			QueueMenuSound(kOkSound);
			BeginApply(a_state);
			break;
		case CaptureAction::kRestore:
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kRestorePopup);
			break;
		case CaptureAction::kExit:
			QueueMenuSound(kCancelSound);
			RequestCancel();
			break;
		case CaptureAction::kNone:
			break;
		}
	}

	// --- Save page ------------------------------------------------------------

	// One full-width option row of a dropdown (Theme::DropdownRow): with `a_showMark` a row of
	// a multi-choice dropdown, with its checkbox. It does not close the combo popup, so the
	// user can tick several rows in one go. Plays the focus sound and returns true when
	// toggled.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool DrawCategoryRow(const char* a_label, bool& a_ticked, bool a_showMark)
	{
		if (!Theme::DropdownRow(a_label, &a_ticked, a_showMark)) {
			return false;
		}
		QueueMenuSound(kFocusSound);
		return true;
	}

	// The actor's rows (request.assigned): the Default, then the situations not removed, named
	// as request.situations names them.
	std::size_t RowCount(const State& a_state)
	{
		return a_state.request.assigned.size();
	}

	const char* RowName(const State& a_state, std::size_t a_row)
	{
		return a_state.request.situations[a_row].name.c_str();
	}

	// The name field of the tab the Save page shows.
	std::array<char, kNameCapacity>& ActiveName(State& a_state)
	{
		return a_state.updateTab ? a_state.updateName : a_state.createName;
	}

	const std::array<char, kNameCapacity>& ActiveName(const State& a_state)
	{
		return a_state.updateTab ? a_state.updateName : a_state.createName;
	}

	// The Create tab's icon for the ticked pieces `a_ticked`: the one chosen in Choose icon,
	// else the one they suggest (OutfitIcons::SuggestKey) in white, which follows the ticks.
	OutfitIcons::Icon CreateIcon(const State& a_state, const std::vector<std::size_t>& a_ticked)
	{
		if (a_state.createIcon) {
			return *a_state.createIcon;
		}
		std::vector<OutfitIcons::PieceKey> pieces;
		pieces.reserve(a_ticked.size());
		for (const auto index : a_ticked) {
			const auto& piece = a_state.request.pieces[index];
			pieces.push_back({ .slots = piece.slots, .key = piece.icon });
		}
		return { .key = std::string{ OutfitIcons::SuggestKey(pieces, Theme::HasOutfitIcon) }, .rgb = OutfitIcons::kDefaultColor };
	}

	// The library outfit the Save page writes: the Update tab's, or 0 for a new one.
	std::uint32_t TargetOutfit(const State& a_state)
	{
		return a_state.updateTab && a_state.chosenOutfit >= 0 ? OutfitAt(a_state, a_state.chosenOutfit).id : 0;
	}

	// Whether row `a_row` of the actor holds library outfit `a_outfit` now (OutfitRows::Holds).
	bool HoldsOutfit(const State& a_state, std::size_t a_row, std::uint32_t a_outfit)
	{
		return OutfitRows::Holds(a_state.request.assigned[a_row].row, a_outfit);
	}

	bool IsTaken(const State& a_state, std::size_t a_row, std::uint32_t a_outfit)
	{
		const auto& [row, pieces] = a_state.request.assigned[a_row];
		if (row.random) {
			return false;  // the outfit joins the list
		}
		return row.chosen != 0 ? row.chosen != a_outfit : pieces;
	}

	// A library outfit as the popups name it, quoted, or says it is gone.
	std::string QuotedOutfitName(const State& a_state, std::uint32_t a_outfit)
	{
		const auto& outfits = a_state.request.existingOutfits;
		const auto  found = std::ranges::find(outfits, a_outfit, &CaptureWindow::ExistingOutfit::id);
		return found != outfits.end() ? std::format("\"{}\"", found->name) : std::string("an outfit no longer in the wardrobe");
	}

	// What row `a_row` wears now with random off, as the replace popup names it.
	std::string HeldOutfitName(const State& a_state, std::size_t a_row)
	{
		const auto outfit = a_state.request.assigned[a_row].row.chosen;
		return outfit ? QuotedOutfitName(a_state, outfit) : std::string("items that are not a saved outfit");
	}

	// "Create New Outfit / Update Existing Outfit" as the page's heading, in the heading
	// font; the second is greyed while the library is empty. Returns 0 or 1 for the tab
	// clicked, else -1.
	int DrawSaveTabs(const State& a_state)
	{
		Theme::BeginHeadingTabs(kSaveTabs);
		int clicked = Theme::TextTab(kSaveTabs[0], !a_state.updateTab) ? 0 : -1;
		Theme::HeadingTabSeparator();
		const bool noOutfits = a_state.request.existingOutfits.empty();
		ImGui::BeginDisabled(noOutfits);
		if (Theme::TextTab(kSaveTabs[1], a_state.updateTab) && !noOutfits) {
			clicked = 1;
		}
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		Theme::EndHeadingTabs();
		// After EndDisabled and EndHeadingTabs, so the tooltip is neither faded nor in the
		// heading font.
		if (noOutfits && hovered) {
			Theme::Tooltip(kNoOutfits);
		}
		return clicked;
	}

	// An outfit's name field (a Save page tab's, the Wardrobe page's), with no label: hovered while
	// not being typed in, it says `Outfit Name`. Returns true when the user pressed Enter with a
	// name typed. The empty field shows kNeedName; a failed save turns it red with the message
	// on hover instead, until the name is edited. With `a_icon`, its icon field comes first on
	// the line: with `a_iconClicked` (the Wardrobe page, the Create tab) it says `Outfit Icon` on
	// hover and `*a_iconClicked` says whether it was clicked; without (the Update tab, which
	// keeps the outfit's icon) it only shows the icon.
	bool DrawNameField(State& a_state, std::array<char, kNameCapacity>& a_name, const OutfitIcons::Icon* a_icon, bool* a_iconClicked, const char* a_hint)
	{
		Theme::SectionGap();  // a field with no label
		if (a_icon) {
			const bool clicked = Theme::IconField("##outfitIcon", RowIconOf(*a_icon), a_iconClicked != nullptr);
			if (a_iconClicked) {
				if (ImGui::IsItemHovered()) {
					Theme::Tooltip("Outfit Icon");
				}
				*a_iconClicked = clicked;
			}
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (a_state.focusNameField) {
			ImGui::SetKeyboardFocusHere();
			a_state.focusNameField = false;
		}
		const bool              failed = !a_state.error.empty();
		const Theme::FieldStyle style{ .look = failed ? Theme::FieldLook::kError : Theme::FieldLook::kNormal };
		const bool              enter = Theme::InputText("##name", a_name.data(), a_name.size(), ImGuiInputTextFlags_EnterReturnsTrue, kNeedName, style);
		if (failed) {
			if (ImGui::IsItemEdited()) {
				a_state.error.clear();
			} else if (ImGui::IsItemHovered()) {
				Theme::Tooltip(a_state.error.c_str());
			}
		} else if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
			Theme::Tooltip(a_hint);
		}
		return enter && !Trimmed(a_name.data()).empty();
	}

	// Makes outfit `a_index` (-1 for none) the Update tab's and shows it on the preview, or the
	// ticks with none; its name fills the tab's field. Choosing the outfit already chosen keeps
	// the name the user typed.
	void ChooseOutfit(State& a_state, int a_index)
	{
		a_state.error.clear();
		if (a_state.chosenOutfit != a_index) {
			a_state.chosenOutfit = a_index;
			a_state.updateName.fill('\0');
			if (a_index >= 0) {
				std::strncpy(a_state.updateName.data(), OutfitAt(a_state, a_index).name.c_str(), kNameCapacity - 1);
			}
		}
		PreviewOutfitOrTicks(a_state, a_index);
	}

	// The Create tab, and the preview back on the ticks; the page's width changes, so its
	// centre is kept.
	void ShowCreateTab(State& a_state)
	{
		a_state.updateTab = false;
		a_state.error.clear();
		if (a_state.chosenOutfit >= 0) {
			QueuePreview(a_state, TickedPieces(a_state));
		}
		HoldCentre(a_state);
	}

	// The Update tab with its outfit, or else the one last loaded, or else the library outfit
	// Livery has on the actor, chosen and on the preview; its centre kept as ShowCreateTab's.
	void ShowUpdateTab(State& a_state)
	{
		a_state.updateTab = true;
		int index = a_state.chosenOutfit;
		if (index < 0) {
			index = a_state.loadedOutfit;
		}
		if (index < 0) {
			index = WornOutfitIndex(a_state);
		}
		ChooseOutfit(a_state, index);
		HoldCentre(a_state);
	}

	struct CreateTabInput
	{
		bool enter = false;        // the name field's Enter
		bool iconClicked = false;  // the icon field, which opens Choose icon
	};

	// The Create tab's fields: the icon (CreateIcon) and name, as the Wardrobe page's, then the
	// ticked pieces read only under their count line, as the Wardrobe page shows an outfit's.
	CreateTabInput DrawCreateTab(State& a_state)
	{
		CreateTabInput input;
		ImGui::PushID("create");
		const auto pieces = TickedPieces(a_state);
		const auto icon = CreateIcon(a_state, pieces);
		input.enter = DrawNameField(a_state, a_state.createName, &icon, &input.iconClicked);
		Theme::AfterFieldGap();
		Theme::CountText(PieceCountText(pieces.size(), 0).c_str());
		DrawPieceNames(a_state, pieces);
		ImGui::PopID();
		return input;
	}

	struct UpdateTabInput
	{
		bool enter = false;        // the name field's Enter
		bool escapeTaken = false;  // Escape cleared the outfit search, so it is not also Back
		int  clicked = -1;         // the outfit clicked: index into request.existingOutfits, -1 for none
	};

	// One piece column of the Update tab: `a_label` where the outfit search is beside it, then
	// the pieces under their count line, as the Load page's details lay them out.
	void DrawPieceColumn(const State& a_state, const char* a_label, const std::vector<std::size_t>& a_pieces, std::size_t a_unloaded)
	{
		ImGui::TableNextColumn();
		ResetCellBaseline();
		ImGui::PushID(a_label);
		Theme::LabelText(a_label);
		Theme::AfterFieldGap();
		Theme::CountText(PieceCountText(a_pieces.size(), a_unloaded).c_str());
		DrawPieceNames(a_state, a_pieces);
		ImGui::PopID();
	}

	// The Update tab's fields: the chosen outfit's icon, shown only, and the name (both greyed,
	// the icon the default one, while no outfit is chosen), then three columns: the wardrobe's
	// outfits with their search and Sort, the chosen outfit's pieces as the library has them,
	// and the ticked pieces that replace them.
	UpdateTabInput DrawUpdateTab(State& a_state)
	{
		UpdateTabInput input;
		const bool     chosen = a_state.chosenOutfit >= 0;
		ImGui::PushID("update");
		if (!chosen) {
			a_state.focusNameField = false;  // a greyed field takes no focus
		}
		const OutfitIcons::Icon icon = chosen ? OutfitAt(a_state, a_state.chosenOutfit).icon : OutfitIcons::Icon{};
		ImGui::BeginDisabled(!chosen);
		input.enter = DrawNameField(a_state, a_state.updateName, &icon);
		ImGui::EndDisabled();
		Theme::AfterFieldGap();
		if (BeginColumns("columns", 3)) {
			ImGui::TableNextColumn();
			const auto controls = DrawBrowserControls(a_state, a_state.updateBrowser);
			input.escapeTaken = controls.escapeTaken;
			input.clicked = DrawOutfitList(a_state, a_state.updateBrowser, a_state.chosenOutfit, controls.height);
			if (chosen) {
				const auto& outfit = OutfitAt(a_state, a_state.chosenOutfit);
				DrawPieceColumn(a_state, "Current Items", outfit.pieces, outfit.unloadedPieces);
			} else {
				ImGui::TableNextColumn();
				ResetCellBaseline();
				Theme::LabelText("Select an outfit to update");
			}
			DrawPieceColumn(a_state, "New Items", TickedPieces(a_state), 0);
			EndColumns(3);
		}
		ImGui::PopID();
		return input;
	}

	enum class SaveAction
	{
		kNone,
		kSave,
		kSaveAndApply,
		kBack
	};

	// Create and Create and Apply, or Update #{Outfit} on the Update tab, and Back, centred
	// below a gap; `a_enter` is the name field's Enter, which is Create or Update. Escape is Back
	// unless a popup is open, which has its own, or the outfit search took it (`a_escapeTaken`).
	SaveAction DrawSaveButtons(const State& a_state, bool a_enter, bool a_escapeTaken)
	{
		constexpr const char* kCreateAndApply = "Create and Apply";

		const bool  chosen = a_state.chosenOutfit >= 0;
		const bool  offersApply = !a_state.updateTab && !a_state.request.saveOnly;
		std::string save = "Create";
		if (a_state.updateTab) {
			save = chosen ? std::format("Update {}", OutfitAt(a_state, a_state.chosenOutfit).name) : std::string("Update");
		}
		if (offersApply) {
			BeginButtonRow({ save.c_str(), kCreateAndApply, "Back" });
		} else {
			BeginButtonRow({ save.c_str(), "Back" });
		}
		const char* reason = nullptr;
		if (a_state.updateTab && !chosen) {
			reason = kNeedOutfit;
		} else if (Trimmed(ActiveName(a_state).data()).empty()) {
			reason = kNeedName;
		}
		const bool pressed = ReasonButton(save.c_str(), reason) || a_enter;
		bool       apply = false;
		if (offersApply) {
			ImGui::SameLine();
			apply = ReasonButton(kCreateAndApply, reason);
		}
		ImGui::SameLine();
		const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		const bool back = Theme::Button("Back") || escape;

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Saving...");
			return SaveAction::kNone;
		}
		if (apply) {
			return SaveAction::kSaveAndApply;
		}
		if (pressed && !reason) {
			return SaveAction::kSave;
		}
		return back ? SaveAction::kBack : SaveAction::kNone;
	}

	// Create and Apply's popup, opened when it asks first (ApplyAsksFirst): the Wardrobe page's
	// Apply popup for the outfit Create makes first (`Create "A", then ...?`). Returns the row
	// the user applies it to, or nullopt; Cancel saves nothing.
	std::optional<std::size_t> DrawCreateApplyPopup(const State& a_state)
	{
		if (!BeginModal(kCreateApplyPopup)) {
			return std::nullopt;
		}
		const auto name = Trimmed(a_state.createName.data());
		const auto row = DrawApplyPopupBody(a_state, { .name = name, .applyLabel = "Create and Apply", .saveFirst = std::format("Create \"{}\"", name) });
		ImGui::EndPopup();
		return row;
	}

	// Update tab: returns true when the user confirmed replacing the outfit's pieces with
	// the ticked ones, and its name with the tab's. No row changes.
	bool DrawConfirmPopup(const State& a_state)
	{
		if (!BeginModal(kConfirmPopup)) {
			return false;
		}
		if (!a_state.updateTab || a_state.chosenOutfit < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		const auto& outfit = OutfitAt(a_state, a_state.chosenOutfit);
		ImGui::TextUnformatted(std::format("Replace the {} item(s) of \"{}\"", outfit.pieces.size(), outfit.name).c_str());
		ImGui::TextUnformatted(std::format("with the {} ticked item(s)?", CountTickedPieces(a_state)).c_str());
		if (outfit.unloadedPieces) {
			ImGui::TextDisabled("%s", std::format("{} item(s) from unloaded plugins stay in it.", outfit.unloadedPieces).c_str());
		}
		if (const auto name = Trimmed(a_state.updateName.data()); name != outfit.name) {
			ImGui::TextUnformatted(std::format("It is renamed \"{}\".", name).c_str());
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

	// Hands the save to onSave; `a_applyRow` (Create and Apply) is the row the new outfit goes
	// to and is worn for, the ticked pieces under Player's Inventory moving first.
	// Contract: the onSave handler writes the library before it assigns anything, and a
	// refused or failed write assigns nothing (CLAUDE.md §2.1).
	void RequestSave(State& a_state, std::optional<std::size_t> a_applyRow)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		CaptureWindow::SaveRequest save{ .outfit = TargetOutfit(a_state),
			.name = Trimmed(ActiveName(a_state).data()),
			.pieces = TickedPieces(a_state),
			.applyRow = a_applyRow };
		if (!a_state.updateTab) {
			save.icon = CreateIcon(a_state, save.pieces);
		}
		if (a_applyRow) {
			save.fromPlayer = TickedFromPlayer(a_state);
		}
		auto onSave = a_state.request.onSave;
		SKSE::GetTaskInterface()->AddTask([onSave, save = std::move(save)] { onSave(save); });
	}

	// Create and Apply gives the new outfit the row the Wardrobe page's Apply would: it asks
	// first when that offers a situation or replaces the Default (ApplyAsksFirst), else
	// saves at once for the Default.
	// Contract: called in the window's own ID scope, as DrawCreateApplyPopup is.
	void BeginCreateAndApply(State& a_state)
	{
		if (ApplyAsksFirst(a_state, 0)) {
			ImGui::OpenPopup(kCreateApplyPopup);
			return;
		}
		RequestSave(a_state, DirectApplyRow(a_state));
	}

	// Reached only with at least one piece ticked, which onSave relies on; the ticks cannot
	// change here. Each tab keeps its name while the window is open, the Update tab its outfit,
	// the Create tab an icon chosen in Choose icon. Create saves at once; Update asks in its
	// confirm popup first.
	void DrawSavePage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		const int      tab = DrawSaveTabs(a_state);
		UpdateTabInput input;
		bool           iconClicked = false;
		if (a_state.updateTab) {
			input = DrawUpdateTab(a_state);
		} else {
			const auto create = DrawCreateTab(a_state);
			input.enter = create.enter;
			iconClicked = create.iconClicked;
		}
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		const auto action = DrawSaveButtons(a_state, input.enter, input.escapeTaken);
		EndButtonRow();
		ImGui::EndDisabled();

		if (const auto row = DrawCreateApplyPopup(a_state)) {
			RequestSave(a_state, *row);
		}
		if (DrawConfirmPopup(a_state)) {
			RequestSave(a_state, std::nullopt);
		}
		if (const auto icon = DrawIconPopup(a_state, true)) {
			a_state.createIcon = *icon;
		}

		if (tab == 0 && a_state.updateTab) {
			QueueMenuSound(kFocusSound);
			ShowCreateTab(a_state);
		} else if (tab == 1 && !a_state.updateTab) {
			QueueMenuSound(kFocusSound);
			ShowUpdateTab(a_state);
		} else if (input.clicked >= 0) {
			ChooseOutfit(a_state, input.clicked == a_state.chosenOutfit ? -1 : input.clicked);
		} else if (iconClicked) {
			QueueMenuSound(kOkSound);
			a_state.iconPicker = { .pick = CreateIcon(a_state, TickedPieces(a_state)) };
			ImGui::OpenPopup(kIconPopup);
		} else if (action == SaveAction::kSave) {
			QueueMenuSound(kOkSound);
			if (a_state.updateTab) {
				ImGui::OpenPopup(kConfirmPopup);
			} else {
				RequestSave(a_state, std::nullopt);
			}
		} else if (action == SaveAction::kSaveAndApply) {
			QueueMenuSound(kOkSound);
			BeginCreateAndApply(a_state);
		} else if (action == SaveAction::kBack) {
			QueueMenuSound(kCancelSound);
			if (a_state.updateTab && a_state.chosenOutfit >= 0) {
				QueuePreview(a_state, TickedPieces(a_state));
			}
			GoToPage(a_state, Page::kCapture);
		}
	}

	// --- Appearance page ------------------------------------------------------

	// The Window skin dropdown, wide enough for its longest palette name, one row per palette;
	// a choice closes it.
	void DrawWindowSkin(const Settings::PaletteChoices& a_palettes, const char* a_source)
	{
		const Theme::FieldStyle style{};
		float                   width = 0.0f;
		for (const auto& label : a_palettes.labels) {
			width = (std::max)(width, Theme::DropdownWidth(label.c_str(), style));
		}
		Theme::LabelText("Window skin");
		ImGui::SetNextItemWidth((std::min)(width, ImGui::GetContentRegionAvail().x));
		const bool open = Theme::BeginCombo("##windowSkin", ImGuiComboFlags_None, style);
		if (open) {
			for (std::size_t i = 0; i < a_palettes.labels.size(); ++i) {
				bool chosen = i == a_palettes.chosen;
				if (Theme::DropdownRow(a_palettes.labels[i].c_str(), &chosen, false)) {
					QueueMenuSound(kFocusSound);
					Settings::ChoosePalette(i, a_source);
					ImGui::CloseCurrentPopup();
				}
			}
		}
		Theme::EndCombo(open, a_palettes.labels[a_palettes.chosen].c_str(), style);
	}

	// `Restore Edge.ini as Livery ships it?`, then Restore / Cancel. Returns true on Restore.
	bool DrawRestorePalettePopup(const char* a_id, const std::string& a_file)
	{
		if (!BeginModal(a_id)) {
			return false;
		}
		ImGui::TextUnformatted(std::format("Restore {}.ini as Livery ships it? Your changes to it will be lost.", a_file).c_str());
		BeginButtonRow({ "Restore", "Cancel" });
		const bool restore = Theme::Button("Restore");
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (restore || cancel) {
			QueueMenuSound(restore ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return restore;
	}

	// The settings page's Appearance section (Settings): Window skin, with Restore original
	// palette at the line's right end while the chosen shipped palette is edited, and Play menu
	// sounds; then Exit, which Escape also is. Nothing here is an edit to save: a choice is
	// written at once, and the next frame's Theme::Apply builds the window in it.
	void DrawAppearancePage(State& a_state)
	{
		constexpr auto kSource = "window";  // as the log names this page
		constexpr auto kRestorePalettePopup = "Restore original palette?###restorePalette";

		const auto palettes = Settings::ListPalettes();
		bool       restore = false;
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		if (!palettes.labels.empty()) {
			DrawWindowSkin(palettes, kSource);
			if (palettes.edited) {
				restore = Theme::LineLink("Restore original palette");
				if (ImGui::IsItemHovered()) {
					Theme::Tooltip("Writes the palette file back as Livery ships it.");
				}
			}
		}
		Theme::SectionGap();  // a field of its own, with no label
		bool sounds = Settings::MenuSounds();
		if (Theme::Checkbox("Play menu sounds", &sounds)) {
			Settings::SetMenuSounds(sounds, kSource);
			QueueMenuSound(kFocusSound);
		}
		if (ImGui::IsItemHovered()) {
			Theme::Tooltip("Plays the game's menu sounds for clicks in Livery.");
		}
		BeginButtonRow({ "Exit" });
		const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		const bool exit = Theme::Button("Exit") || escape;
		EndButtonRow();
		ImGui::EndDisabled();

		if (restore) {
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kRestorePalettePopup);
		}
		if (DrawRestorePalettePopup(kRestorePalettePopup, palettes.file)) {
			Settings::RestoreChosenPalette(kSource);
		}
		if (exit && a_state.status == Status::kEditing) {
			RequestLeave(a_state, { .exit = true });
		}
	}
}

namespace CaptureWindow
{
	using namespace Internal;

	// --- Game thread ----------------------------------------------------------

	void Open(Request a_request)
	{
		auto& state = GetState();
		{
			const std::scoped_lock lock(state.mutex);
			state.request = std::move(a_request);
			state.pieceTicked = std::make_unique<bool[]>(state.request.pieces.size());
			for (std::size_t i = 0; i < state.request.pieces.size(); ++i) {
				state.pieceTicked[i] = state.request.pieces[i].selected;
			}
			// The Update tab's name is filled when an outfit is chosen (ChooseOutfit).
			state.createName.fill('\0');
			std::strncpy(state.createName.data(), state.request.suggestedName.c_str(), kNameCapacity - 1);
			state.createIcon.reset();
			state.updateName.fill('\0');
			state.updateTab = false;
			state.chosenOutfit = -1;
			state.situationsLabel = std::format("SITUATIONS ({})", state.request.actorName);
			state.error.clear();
			state.pending.clear();
			state.focusNameField = false;
			state.status = Status::kEditing;
			state.resetImGui = true;
			state.page = Page::kCapture;
			state.captureTitle = std::format("{}'s Inventory", state.request.actorName);
			state.applyTooltip = std::format("Dress {} in the ticked items.", state.request.actorName);
			SetGroupHeadings(state);
			state.sortColumn = kNameColumn;
			state.sortDescending = false;
			state.showSlots = Settings::ShowSlotColumn();
			SortPieces(state);
			state.previewedOutfit = -1;
			state.previewedChest = -1;
			state.loadTarget = LoadTarget::kInventory;
			state.loadSource = LoadSource::kOutfits;
			state.loadedOutfit = -1;
			state.inventorySources.clear();
			state.inventoryShown.clear();
			state.chest = {};
			for (auto* browser : { &state.loadBrowser, &state.updateBrowser, &state.manageBrowser, &state.situationBrowser }) {
				OutfitBrowser::Reset(*browser, state.request);
			}
			state.manage = {};
			state.discardFor = -1;
			state.leave.reset();
			state.leaveAfterSave = false;
			state.openLeave = false;
			state.situationForm = {};
			FillSituationForm(state, state.request.situations);
			state.situationsTitle = std::format("{}'s Situational Outfits", state.request.actorName);
			state.manageApplyLabel = std::format("Apply to {}", state.request.actorName);
			state.manageApplyTip.clear();
			state.editTicked = std::make_unique<bool[]>(state.request.pieces.size());
			state.editShown.clear();
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
		Overlay::ShowMenu();
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
			state.createName = {};
			state.createIcon.reset();
			state.updateName = {};
			state.manage = {};
			state.situationForm = {};
			state.pending.clear();
		}
		g_open = false;  // before HideMenu: the menu's hide handler tells our hide from the game's by this
		if (frozeGame) {
			SetGameFrozen(false);
		}
		Overlay::HideMenu();
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

	void SetOutfits(OutfitList a_list)
	{
		auto&                  state = GetState();
		const std::scoped_lock lock(state.mutex);
		if (state.status == Status::kClosed) {
			return;
		}
		const auto idOf = [&state](int a_index) { return a_index >= 0 ? OutfitAt(state, a_index).id : std::uint32_t{ 0 }; };
		const auto previewed = idOf(state.previewedOutfit);
		const auto loaded = idOf(state.loadedOutfit);
		const auto chosen = idOf(state.chosenOutfit);
		const auto managed = idOf(state.manage.outfit);
		const auto chestIdOf = [&state](int a_index) { return a_index >= 0 ? ChestAt(state, a_index).id : std::uint32_t{ 0 }; };
		const auto previewedChest = chestIdOf(state.previewedChest);
		const auto editedChest = chestIdOf(state.chest.chest);

		auto& request = state.request;
		request.assigned = std::move(a_list.assigned);
		const auto situationsBefore = std::exchange(request.situations, std::move(a_list.situations));
		request.restoreChanges = std::move(a_list.restoreChanges);
		request.existingOutfits = std::move(a_list.existingOutfits);
		request.chests = std::move(a_list.chests);
		request.categories = std::move(a_list.categories);
		request.actorSummary = std::move(a_list.actorSummary);
		request.dressed = a_list.dressed;
		request.piecesNotWorn = a_list.piecesNotWorn;
		request.extraWorn = a_list.extraWorn;

		state.previewedOutfit = IndexOfOutfit(state, previewed);
		state.previewedChest = IndexOfChest(state, previewedChest);
		state.loadedOutfit = IndexOfOutfit(state, loaded);
		// The chest editor's edits start again from the chest as written; a chest gone (Remove)
		// sends the editor back to the Chests page.
		FillChestForm(state, IndexOfChest(state, editedChest));
		if (state.page == Page::kChestEdit && state.chest.chest < 0) {
			GoToPage(state, Page::kChests);
		}
		const bool redressed = a_list.actorPieces.has_value();
		if (redressed) {
			FollowActorPieces(state, *a_list.actorPieces);
		}
		state.chosenOutfit = IndexOfOutfit(state, chosen);
		if (request.existingOutfits.empty()) {
			state.updateTab = false;  // the Save page's Update tab has no outfit left to list
		}
		for (auto* browser : { &state.loadBrowser, &state.updateBrowser, &state.manageBrowser, &state.situationBrowser }) {
			OutfitBrowser::Rebuild(*browser, request);
		}
		FillSituationForm(state, situationsBefore);
		FollowWornOutfit(state);
		SetGroupHeadings(state);
		FollowInventorySources(state);
		// DrawPieceRows' contract: a piece the Dress page does not list is never ticked.
		const auto inventory = InventoryPieces(state);
		bool       dropped = false;
		for (std::size_t i = 0; i < request.pieces.size(); ++i) {
			if (inventory.ticked[i] && !IsListed(state, inventory, i)) {
				inventory.ticked[i] = false;
				dropped = true;
			}
		}
		FillManageForm(state, IndexOfOutfit(state, managed));
		// The preview follows the ticks when the page shows them and a tick just went.
		const bool managedGone = managed != 0 && state.manage.outfit < 0;
		if (!redressed && (managedGone || (dropped && !PagePreview(state, state.page)))) {
			QueuePreview(state, TickedPieces(state));
		}
		state.discardFor = -1;
		state.status = Status::kEditing;
	}

	void RefusePreview(const std::string& a_reason)
	{
		auto& state = GetState();
		{
			const std::scoped_lock lock(state.mutex);
			if (state.status == Status::kClosed) {
				return;
			}
			state.previewOn = false;
			state.previewBusy = false;
			state.request.previewNote = a_reason;
			state.frozeGame = true;
		}
		SetGameFrozen(true);
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
		FinishLeave(state);  // before the page's width, which a switch changes

		// The width follows the screen height, as the font does, so an ultrawide screen does not stretch the window.
		constexpr float kWindowWidthRatio = 0.80f;  // of the screen height, for the two-column Load, the Wardrobe page and the Situations pages
		constexpr float kWideWidthRatio = 1.05f;    // of the screen height, for the Save page's Update tab and its three columns
		constexpr float kNarrowWidthRatio = 0.53f;  // of the screen height, for the one-column pages
		const bool      twoColumns = state.page == Page::kLoad || state.page == Page::kManage || state.page == Page::kSituations || state.page == Page::kChestEdit;
		float           ratio = twoColumns ? kWindowWidthRatio : kNarrowWidthRatio;
		if (state.page == Page::kSave && state.updateTab) {
			ratio = kWideWidthRatio;
		}
		float width = io.DisplaySize.y * ratio;
		if (state.page == Page::kCapture) {
			width = (std::max)(width, CaptureButtonsWidth() + ImGui::GetStyle().WindowPadding.x * 2.0f);  // a large font's buttons still fit
		} else if (state.page == Page::kSave) {
			width = (std::max)(width, Theme::HeadingTabsWidth(kSaveTabs) + ImGui::GetStyle().WindowPadding.x * 2.0f);  // the heading's tabs fit on one line
		}
		const bool tabs = HasTabs(state.page);
		if (tabs) {
			width += Theme::TabColumnWidth();  // the page keeps its own width beside the column
		}
		const bool pinned = PlaceWindow(state, io, width);
		// Width is fixed every frame; the height follows the content (a zero
		// axis is left to AlwaysAutoResize), so the window never needs to scroll.
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		constexpr auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
		                       ImGuiWindowFlags_NoTitleBar;
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

		// The tab column, then the page in a group beside it, so the page's own widths and
		// centring start after the column; the rule runs the window's height between them.
		const ImVec2 contentTop = ImGui::GetCursorScreenPos();
		if (tabs) {
			DrawTabColumn(state);
			ImGui::SetCursorScreenPos(ImVec2(contentTop.x + Theme::TabColumnWidth(), contentTop.y));
			ImGui::BeginGroup();
		}
		DrawTitle(state);
		switch (state.page) {
		case Page::kCapture:
			DrawCapturePage(state);
			break;
		case Page::kSave:
			DrawSavePage(state);
			break;
		case Page::kLoad:
			DrawLoadPage(state);
			break;
		case Page::kManage:
			DrawManagePage(state);
			break;
		case Page::kEdit:
			DrawEditPage(state);
			break;
		case Page::kSituations:
			DrawSituationPage(state);
			break;
		case Page::kChests:
			DrawChestsPage(state);
			break;
		case Page::kChestEdit:
			DrawChestEditPage(state);
			break;
		case Page::kAppearance:
			DrawAppearancePage(state);
			break;
		}
		if (tabs) {
			ImGui::EndGroup();
			const float padding = ImGui::GetStyle().WindowPadding.y;
			Theme::TabRule(contentTop.x, contentTop.y - padding, ImGui::GetItemRectMax().y + padding);
		}
		ImGui::End();
		ForwardViewInput(state);
	}
}
