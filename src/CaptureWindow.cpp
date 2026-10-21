// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State        - the one State (CaptureWindowInternal.h, with the constants), the game freeze,
//                  menu sounds, text trimming
//   Game thread  - Open / Close / SetError (also freeze and unfreeze the game while Live Preview is off)
//   Input        - QueueInput translates engine events and moves the game's cursor;
//                  FeedInput hands them and the cursor position to ImGui
//   Draw         - page switching, shared list and button-row helpers; the tab column
//                  (Inventory, Manage Wardrobe, Manage Situations) and its rule beside those
//                  pages; leaving a page with unsaved edits through the unsaved-changes alert
//                  (Save Changes, Discard Changes, Back); mouse and key input over the game
//                  goes to onViewInput
//   Capture page - the Inventory page: the actor's pieces under a heading per group (their
//                  Livery outfit, other worn, carried, the player's, the loaded outfit's), in
//                  Name / Type / Category columns sorted by a click on a column label (the piece
//                  list the Edit page shows too, PieceList), each tick handed to
//                  onPieceToggled; under the list the count line, ending in Add pieces from
//                  Wardrobe... (the Load page; greyed while the library has no outfits); the
//                  Live Preview tick, handed to onPreviewOn / onPreviewOff in an SKSE task that
//                  also freezes or unfreezes the game, with Restore Outfit at its line's right
//                  end while Livery dresses the actor; Save / Apply / Cancel; Apply asks first
//                  when the ticks differ from the library outfit Livery has on the actor (Yes
//                  updates it); Apply and Restore hand over to onApply / onRestore
//   Save page    - Create New Outfit / Update Existing Outfit... tabs, each with its own
//                  name, categories, gender and "Situations for #{Name}" rows (Default, the
//                  situations); a popup (Confirm / Cancel) before replacing the outfit a row
//                  wears, the Update tab's confirm popup; both tabs hand over to onSave; ChooseOutfit
//                  fills the Update tab from the Select page's choice
//   Outfit pages - the outfit browser and lists, the Load, Select, Manage Wardrobe and Edit
//                  pages: OutfitPages.cpp; the Manage Situations page: SituationPage.cpp
//   Menu sounds  - the game's menu sounds on Open and on clicks, unless turned off in Settings
#include "PCH.h"

#include "CaptureWindow.h"

#include "CaptureWindowInternal.h"
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

namespace CaptureWindow::Internal
{
	// --- Constants ------------------------------------------------------------

	// The Save page's tabs, Create first, then Update: its heading (DrawSaveTabs), which sets its
	// least width.
	constexpr std::array<const char*, 2> kSaveTabs{ "Create New Outfit", "Update Existing Outfit..." };

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

	// The new page is centred where the current one is (the user may have moved
	// it); PlaceWindow keeps it inside the screen.
	void GoToPage(State& a_state, Page a_page)
	{
		a_state.page = a_page;
		a_state.error.clear();
		a_state.focusNameField = a_page == Page::kSave;
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

	// Ticks of the categories `a_old` moved onto `a_new`, matched by group and name: a
	// category that is gone takes its tick with it, a new one starts unticked.
	std::unique_ptr<bool[]> RemapTicks(const std::vector<CaptureWindow::CategoryOption>& a_old, const bool* a_ticks,
		const std::vector<CaptureWindow::CategoryOption>& a_new)
	{
		auto ticks = std::make_unique<bool[]>(a_new.size());  // all false
		if (!a_ticks) {
			return ticks;
		}
		for (std::size_t i = 0; i < a_new.size(); ++i) {
			const auto found = std::ranges::find_if(a_old, [&](const CaptureWindow::CategoryOption& a_option) {
				return a_option.group == a_new[i].group && a_option.name == a_new[i].name;
			});
			ticks[i] = found != a_old.end() && a_ticks[found - a_old.begin()];
		}
		return ticks;
	}

	// Ticks of the rows `a_old` moved onto `a_new`, matched by situation: a row that is gone
	// (a removed situation) takes its tick with it, a new one (a restored one) starts unticked.
	std::unique_ptr<bool[]> RemapRows(const std::vector<CaptureWindow::SituationInfo>& a_old, const bool* a_ticks,
		const std::vector<CaptureWindow::SituationInfo>& a_new)
	{
		auto ticks = std::make_unique<bool[]>(a_new.size());  // all false
		if (!a_ticks) {
			return ticks;
		}
		for (std::size_t i = 0; i < a_new.size(); ++i) {
			const auto found = std::ranges::find(a_old, a_new[i].situation, &CaptureWindow::SituationInfo::situation);
			ticks[i] = found != a_old.end() && a_ticks[found - a_old.begin()];
		}
		return ticks;
	}

	// The Inventory page's list: its ticks, the loaded outfit's pieces nobody carries under
	// the heading LoadOutfit set.
	PieceList InventoryPieces(const State& a_state)
	{
		const auto* loaded = a_state.loadedOutfit >= 0 ? &OutfitAt(a_state, a_state.loadedOutfit).pieces : nullptr;
		return { a_state.pieceTicked.get(), loaded, &a_state.headings[GroupIndex(CaptureWindow::PieceGroup::kNotCarried)] };
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

	// The Inventory page's ticks, which Save and Apply act on.
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
	// `a_minHeight`: a child of height zero would fill the window.
	// Contract: called with the item spacing the rows will be laid out with.
	float FittedListHeight(float a_rowsHeight, float a_minHeight, float a_reserved)
	{
		const float fullHeight = a_rowsHeight - ImGui::GetStyle().ItemSpacing.y + Theme::ListPaddingY() * 2.0f;
		const float cap = ImGui::GetIO().DisplaySize.y * kMaxListHeightRatio - a_reserved;
		return (std::max)(a_minHeight, (std::min)(fullHeight, cap));
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
			ImGui::SetTooltip("%s", tooltip);
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

	// The Inventory page, Manage Wardrobe and Manage Situations have the tab column at the
	// window's left edge; the pages they lead to do not.
	bool HasTabs(Page a_page)
	{
		return a_page == Page::kCapture || a_page == Page::kManage || a_page == Page::kSituations;
	}

	// Inventory, Manage Wardrobe, then Manage Situations, each centred between the window's
	// left edge and the rule. A click on another tab leaves the page for it (RequestLeave).
	void DrawTabColumn(State& a_state)
	{
		struct TabOption
		{
			Theme::TabIcon icon;
			const char*    name;
			Page           page;
		};
		constexpr std::array<TabOption, 3> kTabs{ {
			{ Theme::TabIcon::kInventory, "Inventory", Page::kCapture },
			{ Theme::TabIcon::kOutfits, "Manage Wardrobe", Page::kManage },
			{ Theme::TabIcon::kSituations, "Manage Situations", Page::kSituations },
		} };

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float  left = origin.x + Theme::TabOffsetX();
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		ImGui::SetCursorScreenPos(ImVec2(left, origin.y + Theme::TabTopGap()));
		for (const auto& tab : kTabs) {
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
		const bool edits = (a_state.page == Page::kManage && HasManageEdits(a_state)) || (a_state.page == Page::kSituations && HasSituationEdits(a_state));
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

	// The pieces a tab page keeps on the preview in place of the Inventory page's ticks:
	// Manage Wardrobe's selected outfit as edited, Manage Situations' outfit whose details
	// show; nullptr while it shows the ticks. A page that gets a preview of its own adds it here.
	const std::vector<std::size_t>* PagePreview(const State& a_state, Page a_page)
	{
		if (a_page == Page::kManage && a_state.manage.outfit >= 0) {
			return &a_state.manage.pieces;
		}
		if (a_page == Page::kSituations) {
			if (const int viewed = IndexOfOutfit(a_state, a_state.situationForm.viewedOutfit); viewed >= 0) {
				return &OutfitAt(a_state, viewed).pieces;
			}
		}
		return nullptr;
	}

	// Every heading of Manage Situations is open when its tab opens, and an outfit's details
	// there close as it goes. The preview shows the opened page's own pieces (PagePreview), or
	// the ticks again when only the page left had its own.
	void Leave(State& a_state, const PendingLeave& a_leave)
	{
		if (a_leave.exit) {
			RequestCancel();
			return;
		}
		const bool ownPreview = PagePreview(a_state, a_state.page) != nullptr;
		a_state.situationForm.viewedOutfit = 0;
		const int target = a_leave.page == Page::kManage ? IndexOfOutfit(a_state, a_leave.manageOutfit) : -1;
		if (target >= 0) {
			// Contract: Manage Wardrobe has no unsaved edits now: leaving it with any went through
			// the unsaved-changes alert, which saved or dropped them.
			FillManageForm(a_state, target);
		}
		if (const auto* pieces = PagePreview(a_state, a_leave.page)) {
			QueuePreview(a_state, *pieces);
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

	LeaveChoice DrawLeavePopup(State& a_state, const std::string& a_text, const char* a_noSave)
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

	// The page's heading: the Inventory page's own title over the summary line; Manage
	// Wardrobe's, Manage Situations', the Select page's and the Edit page's with no summary
	// line; the Load page's the actor's name over the summary line. The Save page's tabs are
	// its heading (DrawSaveTabs).
	void DrawTitle(const State& a_state)
	{
		switch (a_state.page) {
		case Page::kCapture:
			Theme::HeadingText(a_state.captureTitle.c_str(), a_state.request.actorSummary.c_str());
			break;
		case Page::kManage:
			Theme::HeadingText("Manage Wardrobe");
			break;
		case Page::kSituations:
			Theme::HeadingText(a_state.situationsTitle.c_str());
			break;
		case Page::kEdit:
			Theme::HeadingText(a_state.editTitle.c_str());
			break;
		case Page::kSelect:
			Theme::HeadingText("Select an Outfit to Update");
			break;
		case Page::kLoad:
			Theme::HeadingText(a_state.request.actorName.c_str(), a_state.request.actorSummary.c_str());
			break;
		case Page::kSave:
			break;
		}
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

	// Opens a modal popup centred on the screen, in the skin; when it returns true, fill it
	// and call ImGui::EndPopup.
	// Contract: called every frame on the page that opens it: a modal that is open but not
	// submitted stays invisible and still blocks the window.
	bool BeginModal(const char* a_id)
	{
		const auto& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		auto flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
		if (!Theme::HasPopupTitleBar()) {
			flags |= ImGuiWindowFlags_NoTitleBar;
		}
		if (!ImGui::BeginPopupModal(a_id, nullptr, flags)) {
			return false;
		}
		Theme::DecorateWindow(true);
		return true;
	}

	// --- Capture page ---------------------------------------------------------

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
		headings[GroupIndex(Group::kNotInLibrary)] = std::format("From {}'s Outfit (Not in Wardrobe)", name);
		headings[GroupIndex(Group::kUnmanaged)] = std::format("From {}'s Outfit (Unmanaged)", name);
		headings[GroupIndex(Group::kCarried)] = std::format("From {}'s Inventory", name);
		headings[GroupIndex(Group::kPlayer)] = "From Player's Inventory";  // Theme::ListHeadingHelp adds its "(?)"
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

	// SetOutfits after the captured actor was redressed with Live Preview on: the Inventory
	// page lists their pieces as they are now, ticks the ones they wear and lets the loaded
	// outfit go, as if the window had just opened.
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
		a_state.loadedOutfit = -1;
		SortPieces(a_state);
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
		request.outfitName.clear();
		for (auto& piece : request.pieces) {
			if (piece.group == CaptureWindow::PieceGroup::kLiveryOutfit) {
				piece.group = CaptureWindow::PieceGroup::kNotInLibrary;
			}
		}
		SortPieces(a_state);
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

	// A group's heading inside the list: greyed, in the small font, left of the names. The
	// player's group explains on hover what Apply does with its pieces and what it leaves out.
	void DrawGroupHeading(const State& a_state, const PieceList& a_list, CaptureWindow::PieceGroup a_group)
	{
		const bool  notCarried = a_group == CaptureWindow::PieceGroup::kNotCarried;
		const auto& heading = notCarried ? *a_list.heading : a_state.headings[GroupIndex(a_group)];
		if (a_group != CaptureWindow::PieceGroup::kPlayer) {
			Theme::ListHeading(heading.c_str());
			return;
		}
		if (!Theme::ListHeadingHelp(heading.c_str())) {
			return;
		}
		const auto& name = a_state.request.actorName;
		const auto  apply = a_state.request.movesFromPlayer ? std::format("\"Apply\" moves the ticked pieces from your inventory to {}.", name) :
		                                                      std::format("\"Apply\" gives {} a new copy of each ticked piece; yours stay with you.", name);
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
	// Contract: a piece that is not listed is never ticked: on the Inventory page only Load
	// ticks a piece nobody carries, and the next Load sets every tick again; the Edit page
	// lists every edited piece it opened with.
	void DrawPieceRows(State& a_state, const PieceList& a_list)
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
				const std::array<const char*, 3> cells{ pieces[i].name.c_str(), pieces[i].type.c_str(), pieces[i].armorClass.c_str() };
				if (Theme::TickRow(cells, columnX, &a_list.ticked[i])) {
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

	// The count line under the list; on the Inventory and Edit pages (`a_withWardrobeLink`) it
	// ends in `Add pieces from Wardrobe...`, which opens the Load page and is greyed while the
	// library is empty. Returns true when that link is clicked.
	bool DrawPieceCount(const State& a_state, const PieceList& a_list, bool a_withWardrobeLink)
	{
		const auto ticked = Ticked(a_state, a_list).size();
		Theme::LabelText(std::format("{} of {} piece(s) selected", ticked, CountListedPieces(a_state, a_list)).c_str(), false);
		bool add = false;
		if (a_withWardrobeLink) {
			const bool empty = a_state.request.existingOutfits.empty();
			ImGui::BeginDisabled(empty);
			add = Theme::LabelLink("Add pieces from Wardrobe...");
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();
			// After EndDisabled, so the tooltip is not drawn faded.
			if (empty && hovered) {
				ImGui::SetTooltip("%s", kNoOutfits);
			}
		}
		if (ticked == 0) {
			ImGui::TextDisabled(kNeedPiece);
		}
		return add;
	}

	// Restore Outfit and its "(?)" at the right end of the Live Preview line, while Livery
	// dresses the character (Request::onRestore). Returns true when it is clicked.
	// Contract: right after DrawPreviewToggle, on its line.
	bool DrawRestoreLink(const State& a_state)
	{
		if (!a_state.request.onRestore) {
			return false;
		}
		bool       hovered = false;
		const bool restore = Theme::LineHelpLink("Restore Outfit", &hovered);
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

	// The id of the outfit last loaded, or else the one Livery has on the actor, when the
	// ticks are exactly its pieces; 0 when neither is.
	std::uint32_t TickedOutfitId(const State& a_state)
	{
		const auto ticked = TickedPieces(a_state);
		for (const int index : { a_state.loadedOutfit, WornOutfitIndex(a_state) }) {
			if (index >= 0 && ticked == OutfitAt(a_state, index).pieces) {
				return OutfitAt(a_state, index).id;
			}
		}
		return 0;
	}

	// The widest button row of the Capture page, which sets the page's least width.
	float CaptureButtonsWidth()
	{
		return ButtonRowWidth({ "Save", "Apply", "Cancel" });
	}

	enum class CaptureAction
	{
		kNone,
		kLoadPage,
		kSavePage,
		kApply,
		kRestore,
		kCancel
	};

	// Save / Apply / Cancel centred below a gap. Save and Apply act on the ticked pieces, so
	// they need at least one. `a_load`: Add pieces from Wardrobe... was clicked on the count
	// line; `a_restore`: Restore Outfit on the Live Preview line.
	CaptureAction DrawCaptureButtons(const State& a_state, bool a_load, bool a_restore)
	{
		constexpr const char* kSaveTooltip = "Save the ticked pieces as a new outfit, or into an existing one.";

		BeginButtonRow({ "Save", "Apply", "Cancel" });
		const char* noPiece = CountTickedPieces(a_state) == 0 ? kNeedPiece : nullptr;
		const bool  save = ReasonButton("Save", noPiece, kSaveTooltip);
		ImGui::SameLine();
		const bool apply = ReasonButton("Apply", noPiece, a_state.applyTooltip.c_str());
		ImGui::SameLine();
		// While the Apply or Restore popup is open, Escape is its own Cancel.
		const bool cancel = Theme::Button("Cancel") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

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
		if (a_restore) {
			return CaptureAction::kRestore;
		}
		return cancel ? CaptureAction::kCancel : CaptureAction::kNone;
	}

	// `a_updateWorn`: Yes in the Apply popup, so `a_outfit` is the worn library outfit, which
	// the handler updates with the ticks before it dresses the actor.
	void RequestApply(State& a_state, std::uint32_t a_outfit, bool a_updateWorn)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onApply = a_state.request.onApply;
		SKSE::GetTaskInterface()->AddTask(
			[onApply, pieces = TickedPieces(a_state), a_outfit, a_updateWorn] { onApply(pieces, a_outfit, a_updateWorn); });
	}

	// Apply: the popup first when Livery has a library outfit on the actor and the ticks are
	// not exactly its pieces; else the ticks go on at once.
	// Contract: called in the window's own ID scope, as DrawApplyPopup is.
	void BeginApply(State& a_state)
	{
		const int worn = WornOutfitIndex(a_state);
		if (worn >= 0 && TickedPieces(a_state) != OutfitAt(a_state, worn).pieces) {
			ImGui::OpenPopup(kApplyPopup);
			return;
		}
		RequestApply(a_state, TickedOutfitId(a_state), false);
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
		ImGui::TextUnformatted(std::format("Update \"{}\" with the ticked pieces?", outfit.name).c_str());
		if (!request.wornSituation.empty()) {
			ImGui::TextUnformatted(std::format("\"{}\" is {}'s {} outfit.", outfit.name, request.actorName, request.wornSituation).c_str());
		}
		BeginButtonRow({ "Yes", kJustApplyWidth, "Cancel" });
		const bool update = Theme::Button("Yes");
		ImGui::SameLine();
		bool       helpHovered = false;
		const bool justApply = Theme::HelpButton(kJustApply, &helpHovered);
		if (helpHovered) {
			auto help = std::format("The ticked pieces become {}'s Default as they are, without being added to the wardrobe.", request.actorName);
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
		const bool load = DrawPieceCount(a_state, list, true);
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		DrawPreviewToggle(a_state, list);
		const bool restore = DrawRestoreLink(a_state);
		const auto action = DrawCaptureButtons(a_state, load, restore);
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
			a_state.loadIntoEdit = false;
			GoToPage(a_state, Page::kLoad);
			break;
		case CaptureAction::kSavePage:
			QueueMenuSound(kOkSound);
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
		case CaptureAction::kCancel:
			QueueMenuSound(kCancelSound);
			RequestCancel();
			break;
		case CaptureAction::kNone:
			break;
		}
	}

	// --- Save page ------------------------------------------------------------

	std::string JoinNames(const std::vector<std::string>& a_names)
	{
		std::string joined;
		for (const auto& name : a_names) {
			joined += joined.empty() ? "" : ", ";
			joined += name;
		}
		return joined;
	}

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

	SaveForm& ActiveForm(State& a_state)
	{
		return a_state.updateTab ? a_state.updateForm : a_state.createForm;
	}

	const SaveForm& ActiveForm(const State& a_state)
	{
		return a_state.updateTab ? a_state.updateForm : a_state.createForm;
	}

	// The library outfit the Save page writes: the Update tab's, or 0 for a new one.
	std::uint32_t TargetOutfit(const State& a_state)
	{
		return a_state.updateTab ? OutfitAt(a_state, a_state.chosenOutfit).id : 0;
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

	std::vector<std::size_t> TakenRows(const State& a_state, const bool* a_wearsFor, std::uint32_t a_outfit)
	{
		std::vector<std::size_t> rows;
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			if (a_wearsFor[row] && IsTaken(a_state, row, a_outfit)) {
				rows.push_back(row);
			}
		}
		return rows;
	}

	std::vector<std::size_t> AssignedRows(const State& a_state, const bool* a_wearsFor)
	{
		std::vector<std::size_t> rows;
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			if (a_wearsFor[row]) {
				rows.push_back(row);
			}
		}
		return rows;
	}

	std::vector<std::size_t> RemovedRows(const State& a_state, const bool* a_wearsFor, std::uint32_t a_outfit)
	{
		std::vector<std::size_t> rows;
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			if (!a_wearsFor[row] && HoldsOutfit(a_state, row, a_outfit)) {
				rows.push_back(row);
			}
		}
		return rows;
	}

	// `Town: "B" replaces "A"`: a taken row, as the popups name it.
	std::string TakenRowLine(const State& a_state, std::size_t a_row, const std::string& a_name)
	{
		return std::format("{}: \"{}\" replaces {}", RowName(a_state, a_row), a_name, HeldOutfitName(a_state, a_row));
	}

	void DrawRowChangeLines(const State& a_state, const bool* a_wearsFor, std::uint32_t a_outfit, const std::string& a_name)
	{
		const auto taken = TakenRows(a_state, a_wearsFor, a_outfit);
		const auto removed = RemovedRows(a_state, a_wearsFor, a_outfit);
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			if (!a_wearsFor[row]) {
				if (std::ranges::find(removed, row) != removed.end()) {
					ImGui::TextUnformatted(std::format("{}: removed", RowName(a_state, row)).c_str());
				}
			} else if (std::ranges::find(taken, row) != taken.end()) {
				ImGui::TextUnformatted(TakenRowLine(a_state, row, a_name).c_str());
			} else if (!HoldsOutfit(a_state, row, a_outfit)) {
				ImGui::TextUnformatted(std::format("{}: added", RowName(a_state, row)).c_str());
			}
		}
	}

	// The Save page's rows: the active tab's ticks for the outfit it writes.
	std::vector<std::size_t> TakenRows(const State& a_state)
	{
		return TakenRows(a_state, ActiveForm(a_state).wearsFor.get(), TargetOutfit(a_state));
	}

	std::vector<std::size_t> AssignedRows(const State& a_state)
	{
		return AssignedRows(a_state, ActiveForm(a_state).wearsFor.get());
	}

	std::vector<std::size_t> RemovedRows(const State& a_state)
	{
		return RemovedRows(a_state, ActiveForm(a_state).wearsFor.get(), TargetOutfit(a_state));
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
		return outfit ? QuotedOutfitName(a_state, outfit) : std::string("pieces that are not a saved outfit");
	}

	bool IsClass(const CaptureWindow::CategoryOption& a_option)
	{
		return a_option.group == CaptureWindow::kClassGroup;
	}

	// Contract: kClassGroup's categories only. Library::Update replaces only those and keeps
	// the stored categories the window does not offer, and the situations are never stored
	// in the library.
	std::vector<std::string> TickedCategories(const State& a_state, const bool* a_ticked)
	{
		std::vector<std::string> ticked;
		for (std::size_t i = 0; i < a_state.request.categories.size(); ++i) {
			if (IsClass(a_state.request.categories[i]) && a_ticked[i]) {
				ticked.push_back(a_state.request.categories[i].name);
			}
		}
		return ticked;
	}

	const char* GenderName(int a_gender)
	{
		const auto found = std::ranges::find(kGenderOptions, a_gender, &GenderOption::value);
		return found != kGenderOptions.end() ? found->label : "Unisex";
	}

	// "Create New Outfit / Update Existing Outfit..." as the page's heading, in the heading
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
			ImGui::SetTooltip("%s", kNoOutfits);
		}
		return clicked;
	}

	// An outfit's name field (a Save page tab's, Manage Wardrobe'), with no label: hovered while
	// not being typed in, it says `Outfit Name`. Returns true when the user pressed Enter with a
	// name typed. The empty field shows kNeedName; a failed save turns it red with the message
	// on hover instead, until the name is edited.
	bool DrawNameField(State& a_state, std::array<char, kNameCapacity>& a_name)
	{
		Theme::SectionGap();  // a field with no label
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
				ImGui::SetTooltip("%s", a_state.error.c_str());
			}
		} else if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
			ImGui::SetTooltip("Outfit Name");
		}
		return enter && !Trimmed(a_name.data()).empty();
	}

	// The outfit's gender as one row of radio buttons, with no label.
	void DrawGenderField(int& a_gender)
	{
		Theme::SectionGap();  // a field with no label
		for (std::size_t i = 0; i < kGenderOptions.size(); ++i) {
			if (i > 0) {
				ImGui::SameLine();
			}
			if (Theme::RadioButton(kGenderOptions[i].label, a_gender == kGenderOptions[i].value)) {
				a_gender = kGenderOptions[i].value;
				QueueMenuSound(kFocusSound);
			}
		}
	}

	// "Situations for #{Name}" with its "(?)" over the rows' multi-choice dropdown, as on
	// Manage Wardrobe.
	void DrawWearsForField(const State& a_state, SaveForm& a_form)
	{
		if (DrawRowsDropdown(a_state, a_state.situationsLabel.c_str(), a_form.wearsFor.get())) {
			DrawHelpTooltip(std::format(
				"Situational outfit. {0} changes into it whenever a ticked situation applies. Default is what {0} "
				"wears wherever no situation applies.",
				a_state.request.actorName));
		}
	}

	enum class SaveAction
	{
		kNone,
		kSave,
		kBack
	};

	// Create, or Update #{Outfit} on the Update tab, and Back, centred below a gap; `a_enter`
	// is the name field's Enter. Escape is Back unless a popup is open, which has its own.
	SaveAction DrawSaveButtons(const State& a_state, bool a_enter)
	{
		const std::string save = a_state.updateTab ? std::format("Update {}", OutfitAt(a_state, a_state.chosenOutfit).name) : std::string("Create");
		BeginButtonRow({ save.c_str(), "Back" });
		const bool valid = !Trimmed(ActiveForm(a_state).name.data()).empty();
		const bool pressed = ReasonButton(save.c_str(), valid ? nullptr : kNeedName) || a_enter;
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

		if (a_state.status != Status::kEditing) {
			ImGui::TextDisabled("Saving...");
			return SaveAction::kNone;
		}
		if (pressed && valid) {
			return SaveAction::kSave;
		}
		return back ? SaveAction::kBack : SaveAction::kNone;
	}

	// The ticked rows whose worn outfit the save replaces, one line each (`Town: "B" replaces
	// "A"`), then Confirm or Cancel. The replaced outfit leaves the row's list (OutfitRows::Give).
	// Returns true on Confirm.
	bool DrawReplacePopup(const State& a_state)
	{
		if (!BeginModal(kReplacePopup)) {
			return false;
		}
		const auto rows = TakenRows(a_state);
		if (rows.empty()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		const auto name = Trimmed(ActiveForm(a_state).name.data());
		ImGui::TextUnformatted(rows.size() == 1 ? "This already has an outfit:" : "These already have an outfit:");
		for (const auto row : rows) {
			ImGui::TextUnformatted(TakenRowLine(a_state, row, name).c_str());
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

	// Update tab: returns true when the user confirmed replacing the outfit's pieces and
	// classes with the ticked ones, and its name, gender and rows as the tab has them.
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
		const auto& form = a_state.updateForm;
		ImGui::TextUnformatted(std::format("Replace the {} piece(s) of \"{}\"", outfit.pieces.size(), outfit.name).c_str());
		ImGui::TextUnformatted(std::format("with the {} ticked piece(s)?", CountTickedPieces(a_state)).c_str());
		if (outfit.unloadedPieces) {
			ImGui::TextDisabled("%s", std::format("{} piece(s) from unloaded plugins stay in it.", outfit.unloadedPieces).c_str());
		}
		if (const auto classes = TickedCategories(a_state, form.classTicked.get()); classes.empty()) {
			ImGui::TextUnformatted("It will have no Category.");
		} else {
			ImGui::TextUnformatted(std::format("Its Category becomes: {}.", JoinNames(classes)).c_str());
		}
		if (const auto name = Trimmed(form.name.data()); name != outfit.name) {
			ImGui::TextUnformatted(std::format("It is renamed \"{}\".", name).c_str());
		}
		if (form.gender != outfit.gender) {
			ImGui::TextUnformatted(std::format("Its gender becomes {}.", GenderName(form.gender)).c_str());
		}
		// The replace popup came first, so the taken rows are kept or already agreed.
		DrawRowChangeLines(a_state, form.wearsFor.get(), outfit.id, Trimmed(form.name.data()));
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

	// Contract: the onSave handler writes the library before it assigns anything, and a
	// refused or failed write assigns nothing (CLAUDE.md §2.1).
	void RequestSave(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		const auto&                form = ActiveForm(a_state);
		CaptureWindow::SaveRequest save{ .outfit = TargetOutfit(a_state),
			.name = Trimmed(form.name.data()),
			.gender = form.gender,
			.pieces = TickedPieces(a_state),
			.categories = TickedCategories(a_state, form.classTicked.get()),
			.assign = AssignedRows(a_state),
			.remove = a_state.updateTab ? RemovedRows(a_state) : std::vector<std::size_t>{} };
		auto                       onSave = a_state.request.onSave;
		SKSE::GetTaskInterface()->AddTask([onSave, save = std::move(save)] { onSave(save); });
	}

	// After the replace popup, or with no row to take over: Create saves at once, the Update
	// tab first asks in its confirm popup, which opens a frame later in the page's ID scope.
	void ContinueSave(State& a_state)
	{
		if (a_state.updateTab) {
			a_state.openConfirm = true;
		} else {
			RequestSave(a_state);
		}
	}

	// Contract: called in the window's own ID scope, as DrawReplacePopup is.
	void BeginSave(State& a_state)
	{
		if (!TakenRows(a_state).empty()) {
			ImGui::OpenPopup(kReplacePopup);
			return;
		}
		ContinueSave(a_state);
	}

	// The Update tab's outfit selection, with the chosen outfit (or else the one last loaded)
	// selected and on the preview.
	void OpenOutfitSelection(State& a_state)
	{
		a_state.selectingOutfit = a_state.chosenOutfit >= 0 ? a_state.chosenOutfit : a_state.loadedOutfit;
		if (a_state.selectingOutfit >= 0) {
			PreviewOutfitOrTicks(a_state, a_state.selectingOutfit);
		}
		GoToPage(a_state, Page::kSelect);
	}

	// Reached only with at least one piece ticked, which onSave relies on; the ticks cannot
	// change here. Each tab keeps what was typed and ticked on it while the window is open.
	void DrawSavePage(State& a_state)
	{
		if (std::exchange(a_state.openConfirm, false)) {
			ImGui::OpenPopup(kConfirmPopup);
		}
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		const int tab = DrawSaveTabs(a_state);
		auto&     form = ActiveForm(a_state);
		ImGui::PushID(a_state.updateTab ? "update" : "create");
		const bool enter = DrawNameField(a_state, form.name);
		DrawGenderField(form.gender);
		DrawCategoryDropdown(a_state, form.classTicked.get());
		DrawWearsForField(a_state, form);
		ImGui::PopID();
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		const auto action = DrawSaveButtons(a_state, enter);
		EndButtonRow();
		ImGui::EndDisabled();

		if (DrawReplacePopup(a_state)) {
			ContinueSave(a_state);
		}
		if (DrawConfirmPopup(a_state)) {
			RequestSave(a_state);
		}

		if (tab == 0 && a_state.updateTab) {
			QueueMenuSound(kFocusSound);
			a_state.updateTab = false;
			a_state.error.clear();
		} else if (tab == 1) {
			QueueMenuSound(kOkSound);
			OpenOutfitSelection(a_state);
		} else if (action == SaveAction::kSave) {
			QueueMenuSound(kOkSound);
			BeginSave(a_state);
		} else if (action == SaveAction::kBack) {
			QueueMenuSound(kCancelSound);
			GoToPage(a_state, Page::kCapture);
		}
	}

	// Fills the Update tab from outfit `a_index`: its name, classes and gender, and the rows
	// the actor has it for. Choosing the outfit already chosen keeps the user's edits.
	void ChooseOutfit(State& a_state, int a_index)
	{
		a_state.updateTab = true;
		a_state.error.clear();
		if (a_state.chosenOutfit == a_index) {
			return;
		}
		a_state.chosenOutfit = a_index;
		const auto& outfit = OutfitAt(a_state, a_index);
		const auto& options = a_state.request.categories;
		auto&       form = a_state.updateForm;
		form.name.fill('\0');
		std::strncpy(form.name.data(), outfit.name.c_str(), kNameCapacity - 1);
		for (std::size_t i = 0; i < options.size(); ++i) {
			form.classTicked[i] = IsClass(options[i]) && std::ranges::find(outfit.categories, options[i].name) != outfit.categories.end();
		}
		form.gender = outfit.gender;
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			form.wearsFor[row] = HoldsOutfit(a_state, row, outfit.id);
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
			const auto categories = state.request.categories.size();
			const auto rows = state.request.assigned.size();
			// Create starts from the suggestions and no row ticked (CLAUDE.md §2.1); the
			// Update tab is filled when an outfit is chosen (ChooseOutfit).
			state.createForm = { .classTicked = std::make_unique<bool[]>(categories),
				.gender = state.request.suggestedGender,
				.wearsFor = std::make_unique<bool[]>(rows) };
			std::strncpy(state.createForm.name.data(), state.request.suggestedName.c_str(), kNameCapacity - 1);
			for (std::size_t i = 0; i < categories; ++i) {
				state.createForm.classTicked[i] = IsClass(state.request.categories[i]) && state.request.categories[i].selected;
			}
			state.updateForm = { .classTicked = std::make_unique<bool[]>(categories), .wearsFor = std::make_unique<bool[]>(rows) };
			state.updateTab = false;
			state.chosenOutfit = -1;
			state.selectingOutfit = -1;
			state.openConfirm = false;
			state.situationsLabel = std::format("Situations for {}", state.request.actorName);
			state.error.clear();
			state.pending.clear();
			state.focusNameField = false;
			state.status = Status::kEditing;
			state.resetImGui = true;
			state.page = Page::kCapture;
			state.captureTitle = std::format("{}'s Inventory", state.request.actorName);
			state.applyTooltip = std::format("Dress {} in the ticked pieces.", state.request.actorName);
			SetGroupHeadings(state);
			state.sortColumn = kNameColumn;
			state.sortDescending = false;
			SortPieces(state);
			state.previewedOutfit = -1;
			state.loadedOutfit = -1;
			for (auto* browser : { &state.loadBrowser, &state.selectBrowser, &state.manageBrowser, &state.situationBrowser }) {
				OutfitBrowser::Reset(*browser, state.request);
			}
			state.showTags = true;
			state.manage = { .classTicked = std::make_unique<bool[]>(categories), .wearsFor = std::make_unique<bool[]>(rows) };
			state.discardFor = -1;
			state.leave.reset();
			state.leaveAfterSave = false;
			state.openLeave = false;
			state.situationForm = {};
			FillSituationForm(state, state.request.situations);
			state.situationsTitle = std::format("{}'s Situational Outfits", state.request.actorName);
			state.manageApplyLabel = std::format("Apply to {}", state.request.actorName);
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
			state.createForm = {};
			state.updateForm = {};
			state.manage = {};
			state.situationForm = {};
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
		const auto selecting = idOf(state.selectingOutfit);
		const auto managed = idOf(state.manage.outfit);

		auto& request = state.request;
		for (auto* ticks : { &state.createForm.classTicked, &state.updateForm.classTicked, &state.manage.classTicked }) {
			*ticks = RemapTicks(request.categories, ticks->get(), a_list.categories);
		}
		for (auto* ticks : { &state.createForm.wearsFor, &state.updateForm.wearsFor, &state.manage.wearsFor }) {
			*ticks = RemapRows(request.situations, ticks->get(), a_list.situations);
		}
		const auto assignedBefore = std::exchange(request.assigned, std::move(a_list.assigned));
		const auto situationsBefore = std::exchange(request.situations, std::move(a_list.situations));
		request.restoreChanges = std::move(a_list.restoreChanges);
		request.existingOutfits = std::move(a_list.existingOutfits);
		request.categories = std::move(a_list.categories);
		request.actorSummary = std::move(a_list.actorSummary);

		state.previewedOutfit = IndexOfOutfit(state, previewed);
		state.loadedOutfit = IndexOfOutfit(state, loaded);
		const bool redressed = a_list.actorPieces.has_value();
		if (redressed) {
			FollowActorPieces(state, *a_list.actorPieces);
		}
		state.chosenOutfit = IndexOfOutfit(state, chosen);
		state.selectingOutfit = IndexOfOutfit(state, selecting);
		if (chosen != 0 && state.chosenOutfit < 0) {
			state.updateTab = false;  // the Save page's Update tab has no outfit left to update
		}
		// The Update tab's "Situations for #{Name}" ticks follow the rows Manage Wardrobe or Manage
		// Situations changed for its outfit, so its Update cannot undo them unseen; the user's
		// other ticks stay. Rows are matched by situation, as one may have been removed.
		for (std::size_t row = 0; state.chosenOutfit >= 0 && row < RowCount(state); ++row) {
			const auto found = std::ranges::find(situationsBefore, request.situations[row].situation, &SituationInfo::situation);
			const auto rowBefore = static_cast<std::size_t>(found - situationsBefore.begin());
			const bool before = rowBefore < assignedBefore.size() && OutfitRows::Holds(assignedBefore[rowBefore].row, chosen);
			const bool now = HoldsOutfit(state, row, chosen);
			if (before != now) {
				state.updateForm.wearsFor[row] = now;
			}
		}
		for (auto* browser : { &state.loadBrowser, &state.selectBrowser, &state.manageBrowser, &state.situationBrowser }) {
			OutfitBrowser::Rebuild(*browser, request);
		}
		FillSituationForm(state, situationsBefore);
		FollowWornOutfit(state);
		SetGroupHeadings(state);
		if (state.loadedOutfit >= 0) {
			state.headings[GroupIndex(PieceGroup::kNotCarried)] = std::format("From \"{}\" (Not Carried)", OutfitAt(state, state.loadedOutfit).name);
		}
		// DrawPieceRows' contract: a piece the Inventory page does not list is never ticked.
		const auto inventory = InventoryPieces(state);
		for (std::size_t i = 0; i < request.pieces.size(); ++i) {
			if (inventory.ticked[i] && !IsListed(state, inventory, i)) {
				inventory.ticked[i] = false;
			}
		}
		FillManageForm(state, IndexOfOutfit(state, managed));
		if (managed != 0 && state.manage.outfit < 0 && !redressed) {
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
		constexpr float kWindowWidthRatio = 0.80f;  // of the screen height, for the two-column Load, Select, Manage Wardrobe and Manage Situations pages
		constexpr float kNarrowWidthRatio = 0.53f;  // of the screen height, for the one-column pages
		const bool      twoColumns = state.page == Page::kLoad || state.page == Page::kSelect || state.page == Page::kManage || state.page == Page::kSituations;
		float           width = io.DisplaySize.y * (twoColumns ? kWindowWidthRatio : kNarrowWidthRatio);
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
		case Page::kSelect:
			DrawSelectPage(state);
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
