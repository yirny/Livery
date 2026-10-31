// SPDX-License-Identifier: GPL-3.0-or-later
//
// The capture window's outfit pages (see CaptureWindow.cpp for the window and the
// Inventory and Save pages, SituationPage.cpp for the Situations page, CaptureWindowInternal.h
// for the state they share).
//
// FILE MAP
//   Outfit browser - Search (and Filter, hidden by kShowFilter), above an outfit page's
//                  list in its column, in the small font (OutfitBrowser.cpp narrows and
//                  orders it); each page has its own; Escape first clears the search
//   Outfit lists - the library outfit list under its icon and Name column labels, which sort
//                  it (a second click reverses), its count line below it, the selected outfit's name
//                  (or a hint while none is) and its piece list, and an outfit read only
//                  in the Wardrobe page's order, which the outfit pages and the Situations page
//                  share (there each row has a grip and drags a copy of its outfit);
//                  on the Load page and the Save page's Update tab the selected outfit is on
//                  the preview, and a second click on it, or Back, shows the ticks again
//   Load page    - library outfits and the selected one's details; Load ticks its
//                  pieces and returns (greyed while none of them is loaded); opened from
//                  the Edit page, it adds them to that page's ticks; opened by Add from
//                  Chest..., it lists the chests instead (ChestPages.cpp) and Load lists
//                  the selected one's items, ticking none; opened from the chest editor, Add
//                  puts the selection's items in the chest
//   the Wardrobe page - the tab beside the Inventory page: library outfits and the selected
//                  one's icon (Choose icon popup), name and pieces (ticked as Apply would
//                  dress the actor in them, from the row they wear it for or the Default),
//                  edited in place and kept for one outfit at a time (Discard changes / Keep
//                  editing before another is selected); the edited outfit, less its unticked
//                  pieces, on the preview; Update lists the changes
//                  (Save changes?; Confirm / Cancel) and hands them to onManageUpdate, Remove
//                  names who has the outfit (Remove outfit?) and hands it to onManageRemove,
//                  Apply to #{Name} changes what the actor wears now: the situation outfit
//                  they wear, when they agree in Apply outfit? (Yes), else the Default (Apply
//                  as Default; Apply outfit? first when it saves edits or replaces a Default),
//                  through onManageApply or onManageUpdate (the popup's body is shared with
//                  the Save page's Create and Apply, ApplySubject); Exit closes the window;
//                  Exit, Escape or another tab with unsaved edits shows the unsaved-changes
//                  alert, whose Save Changes goes through Update's popup
//   Edit page    - the edited pieces as the Inventory page's piece list with its own ticks,
//                  Add from Wardrobe... and Live Preview; Use Ticked Pieces makes
//                  them the edited pieces, Back leaves them; nothing is written
#include "PCH.h"

#include "CaptureWindowInternal.h"

#include "OutfitBrowser.h"
#include "sarrai/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CaptureWindow::Internal
{
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
	void DrawSearchField(OutfitBrowser::State& a_browser, BrowserInput& a_input)
	{
		a_input.escapeTaken = OutfitBrowser::IsSearching(a_browser) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
		                      !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

		const Theme::FieldStyle style{ .look = OutfitBrowser::IsSearching(a_browser) ? Theme::FieldLook::kHighlighted : Theme::FieldLook::kNormal,
			.shape = Theme::FieldShape::kUnderlined,
			.icon = Theme::FieldIcon::kSearch };
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::SetNextItemAllowOverlap();  // the clear button sits on its right end
		a_input.changed |= Theme::InputText("##search", a_browser.search.data(), a_browser.search.size(), ImGuiInputTextFlags_EscapeClearsAll,
			"Search by outfit name", style);
		bool clear = a_input.escapeTaken;
		if (OutfitBrowser::IsSearching(a_browser)) {
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
			OutfitBrowser::ClearSearch(a_browser);
			a_input.changed = true;
		}
	}

	// The Filter dropdown's rows under a grey heading per group, lined up with
	// the rows' text.
	void DrawFilterRows(OutfitBrowser::State& a_browser, BrowserInput& a_input)
	{
		const auto& style = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));  // rows carry their own gap
		for (std::size_t i = 0; i < a_browser.filter.size(); ++i) {
			const auto& row = a_browser.filter[i];
			if (i == 0 || row.group != a_browser.filter[i - 1].group) {
				if (i > 0) {
					ImGui::Dummy(ImVec2(0.0f, style.ItemInnerSpacing.y));
				}
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.FramePadding.x);
				ImGui::TextDisabled("%s", row.group.c_str());
			}
			ImGui::PushID(static_cast<int>(i));
			a_input.changed |= DrawCategoryRow(row.label.c_str(), a_browser.filterTicked[i]);
			ImGui::PopID();
		}
		ImGui::PopStyleVar();
	}

	// Under the search: an underlined multi-select dropdown with a funnel,
	// its rule in the accent while a row is ticked, narrowed by the clear
	// button beside it so the button keeps clear of the skin's arrow. With
	// nothing ticked it shows a greyed hint and the button is greyed out.
	void DrawFilterField(OutfitBrowser::State& a_browser, BrowserInput& a_input)
	{
		const bool              filtering = OutfitBrowser::IsFiltering(a_browser);
		const Theme::FieldStyle style{ .look = filtering ? Theme::FieldLook::kHighlighted : Theme::FieldLook::kNormal,
			.shape = Theme::FieldShape::kUnderlined,
			.icon = Theme::FieldIcon::kFilter,
			.previewIsHint = !filtering };
		const float             side = Theme::FieldHeight(style);
		const float             gap = ImGui::GetStyle().ItemInnerSpacing.x;
		ImGui::SetNextItemWidth((std::max)(0.0f, ImGui::GetContentRegionAvail().x - side - gap));
		const std::string preview = filtering ? OutfitBrowser::FilterPreview(a_browser) : "Filter by category";
		const bool        open = Theme::BeginCombo("##filter", ImGuiComboFlags_HeightLargest, style);
		if (open) {
			DrawFilterRows(a_browser, a_input);
		}
		Theme::EndCombo(open, preview.c_str(), style);

		ImGui::SameLine(0.0f, gap);
		ImGui::BeginDisabled(!filtering);
		if (Theme::ClearButton("##clearFilter", side)) {
			QueueMenuSound(kFocusSound);
			OutfitBrowser::ClearFilter(a_browser);
			a_input.changed = true;
		}
		ImGui::EndDisabled();
	}

	// Search over Filter (while kShowFilter), in the small font, with no labels, at the top
	// of the outfit list's column; refreshes the list only when one of them changed.
	BrowserControls DrawBrowserControls(const State& a_state, OutfitBrowser::State& a_browser)
	{
		constexpr float kGapBelowRow = 4.0f;  // pixels at 1080p, under the row on top of the item spacing

		const float top = ImGui::GetCursorPosY();
		Theme::SectionGap();  // the heading leaves its last gap to the page's first field (Theme::HeadingText)
		BrowserInput input;
		ImGui::PushID("browser");
		Theme::PushSmallFont();
		DrawSearchField(a_browser, input);
		if (kShowFilter) {
			DrawFilterField(a_browser, input);
		}
		Theme::PopSmallFont();
		ImGui::PopID();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kGapBelowRow * Theme::Scale());
		if (input.changed) {
			OutfitBrowser::Refresh(a_browser, a_state.request);
		}
		return { input.escapeTaken, ImGui::GetCursorPosY() - top };
	}

	// --- Outfit lists ---------------------------------------------------------

	// Height of the outfit list with every library outfit in view, capped as ListHeight caps
	// it with the count line below it kept in view too, so narrowing the list does not move
	// the buttons.
	// Contract: called with the list's row spacing pushed.
	float OutfitListHeight(const State& a_state, float a_reserved)
	{
		Theme::PushSmallFont();
		const float countLine = ImGui::GetTextLineHeightWithSpacing();
		Theme::PopSmallFont();
		const float rowHeight = Theme::OutfitRowHeight();
		const float rows = static_cast<float>(a_state.request.existingOutfits.size()) * rowHeight;
		return FittedListHeight((std::max)(rows, rowHeight), rowHeight, a_reserved + countLine);
	}

	// One outfit of the list: its name, and at its right end the marks of the outfit the actor
	// wears now or has as their Default and of one chosen for a situation (Theme::OutfitMarks), the last with `a_situationMark` only;
	// `a_striped` for every second row.
	bool DrawOutfitRow(const State& a_state, const OutfitBrowser::State& a_browser, int a_index, bool a_selected, bool a_striped, Theme::Grip a_grip,
		bool a_situationMark)
	{
		const auto& outfit = OutfitAt(a_state, a_index);
		// On the Situations page the Default and situation marks follow the page's unsaved rows
		// and names; the current mark is what the actor wears, so it stays the request's.
		const auto&        entries = a_state.situationForm.entries;
		const bool         edited = a_state.page == Page::kSituations && !entries.empty();
		std::string        editedSituations;
		const std::string* situations = &a_browser.situations[static_cast<std::size_t>(a_index)];
		if (edited && a_situationMark) {
			editedSituations = EditedSituationsText(a_state, outfit.id);
			situations = &editedSituations;
		}
		const Theme::OutfitMarks marks{ .current = outfit.id != 0 && outfit.id == a_state.request.wornOutfit,
			.defaultOutfit = edited ? OutfitRows::Holds(entries[0].row, outfit.id) : HoldsOutfit(a_state, 0, outfit.id),
			.situations = a_situationMark && !situations->empty() ? situations->c_str() : nullptr };
		const auto               icon = RowIconOf(outfit.icon);
		return Theme::OutfitRow(outfit.name.c_str(), a_selected, marks, a_striped, a_grip, &icon);
	}

	// Left column of the outfit pages: the wardrobe's outfits that pass the
	// browser, in its order, `a_selected` highlighted, under the icon and Name column labels
	// and over the count line. A click on a label sorts by its column, a second one reverses
	// the order, as the piece list's labels do. The list stays as tall as the whole wardrobe,
	// so narrowing it does not move the buttons; `a_reserved` is the browser's height (ListHeight).
	// Returns the index into request.existingOutfits of the one clicked, or -1;
	// a click plays the focus sound. `a_draggable` (the Situations page): each row has a grip
	// and drags a copy of its outfit.
	// Names are shown as they are (Theme::SelectRow).
	int DrawOutfitList(State& a_state, OutfitBrowser::State& a_browser, int a_selected, float a_reserved, bool a_draggable)
	{
		constexpr std::array<const char*, 2> kLabels{ "##iconColumn", "Name" };  // in OutfitBrowser::SortKey's order

		const auto& outfits = a_state.request.existingOutfits;
		const auto& visible = a_browser.visible;
		const auto  grip = a_draggable ? Theme::Grip::kEdge : Theme::Grip::kNone;
		const auto  columns = Theme::OutfitColumns(grip);
		const int   sorted = static_cast<int>(a_browser.sort);
		if (const int column = Theme::HeaderRow(kLabels, columns, sorted, a_browser.descending, "Sort by icon"); column >= 0) {
			QueueMenuSound(kFocusSound);
			OutfitBrowser::SetSort(a_browser, a_state.request, static_cast<OutfitBrowser::SortKey>(column), column == sorted && !a_browser.descending);
		}
		// No gap between rows, so every other row's stripe meets the next; the rows hold the gap
		// themselves (Theme::OutfitRow). Popped before EndList, as the piece list's spacing is.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
		const float listHeight = OutfitListHeight(a_state, a_reserved);
		int         clicked = -1;
		if (Theme::BeginList("outfits", listHeight)) {
			if (visible.empty()) {
				Theme::HintRow("No outfit matches.");
			}
			for (std::size_t drawn = 0; drawn < visible.size(); ++drawn) {
				const int index = visible[drawn];
				ImGui::PushID(index);
				if (DrawOutfitRow(a_state, a_browser, index, a_selected == index, drawn % 2 == 1, grip)) {
					clicked = index;
				}
				if (a_draggable) {
					const auto& outfit = OutfitAt(a_state, index);
					OfferOutfitDrag({ .outfit = outfit.id }, outfit.name.c_str(), grip, &outfit.icon);
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
		if (visible.size() == outfits.size()) {
			Theme::CountText(std::format("{} outfit(s) in the wardrobe", outfits.size()).c_str(), false);
		} else {
			Theme::CountText(std::format("{} of {} outfits", visible.size(), outfits.size()).c_str(), false);
		}
		if (clicked >= 0) {
			QueueMenuSound(kFocusSound);
		}
		return clicked;
	}

	// Top of the right column, level with the search field: the selected
	// outfit's icon and name, so the selection stays named while the browser hides it
	// from the list. With none selected it shows `a_hint` as a field label and
	// returns false.
	bool DrawSelectedName(const State& a_state, int a_selected, const char* a_hint)
	{
		if (a_selected < 0) {
			Theme::LabelText(a_hint);
			return false;
		}
		Theme::SectionGap();  // as the label beside it opens (Theme::LabelText)
		const auto& outfit = OutfitAt(a_state, a_selected);
		Theme::OutfitNameText(outfit.name.c_str(), RowIconOf(outfit.icon));
		return true;
	}

	// The pieces whose plugin is not loaded stay in the outfit but have no row.
	std::string PieceCountText(std::size_t a_loaded, std::size_t a_unloaded)
	{
		const auto total = a_loaded + a_unloaded;
		return a_unloaded ? std::format("{} item(s), {} not loaded", total, a_unloaded) : std::format("{} item(s)", total);
	}

	void DrawPieceNames(const State& a_state, const std::vector<std::size_t>& a_pieces, float a_bottom)
	{
		PushListRowSpacing();  // as the outfit list beside it
		const float rowHeight = Theme::SelectRowHeight() + ImGui::GetStyle().ItemSpacing.y;
		const float room = a_bottom - ImGui::GetCursorScreenPos().y;
		const float listHeight = (std::max)(rowHeight, (std::min)(ListHeight(a_pieces.size(), rowHeight), room));
		if (Theme::BeginList("outfitPieces", listHeight)) {
			for (const auto index : a_pieces) {
				const auto&          piece = a_state.request.pieces[index];
				const Theme::RowIcon icon{ piece.icon, piece.iconColor };  // the Inventory page's
				const bool           faded = FadesNotCarried(a_state, index);
				if (faded) {
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kNotCarriedAlpha);
				}
				Theme::TextRow(piece.name.c_str(), &icon);
				if (faded) {
					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered()) {
						Theme::Tooltip(kNotCarriedHint);
					}
				}
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
	}

	bool DrawPieceChoices(const State& a_state, const std::vector<std::size_t>& a_pieces, std::vector<std::uint32_t>& a_excluded, float a_bottom)
	{
		const auto& pieces = a_state.request.pieces;
		const auto  isOff = [&](std::size_t a_index) { return std::ranges::binary_search(a_excluded, pieces[a_index].armor); };
		const auto  ticked = std::ranges::count_if(a_pieces, [&](std::size_t a_index) { return !isOff(a_index); });
		bool        changed = false;
		PushListRowSpacing();                                                               // as the outfit list beside it
		const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;  // Theme::TickRow's
		const float room = a_bottom - ImGui::GetCursorScreenPos().y;
		const float listHeight = (std::max)(rowHeight, (std::min)(ListHeight(a_pieces.size(), rowHeight), room));
		if (Theme::BeginList("outfitPieces", listHeight)) {
			const std::array<float, 1> columnX{ Theme::RowIconRoom() };
			for (const auto i : a_pieces) {
				bool                             on = !isOff(i);
				const std::array<const char*, 1> cells{ pieces[i].name.c_str() };
				ImGui::PushID(static_cast<int>(i));
				const bool faded = FadesNotCarried(a_state, i);
				if (faded) {
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kNotCarriedAlpha);
				}
				// TickRow flips `on`; the last ticked piece stays ticked.
				const bool toggled = Theme::TickRow(cells, columnX, &on, { pieces[i].icon, pieces[i].iconColor });
				if (faded) {
					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered()) {
						Theme::Tooltip(kNotCarriedHint);
					}
				}
				if (toggled && (on || ticked > 1)) {
					QueueMenuSound(kFocusSound);
					const auto at = std::ranges::lower_bound(a_excluded, pieces[i].armor);
					if (on) {
						a_excluded.erase(at);
					} else {
						a_excluded.insert(at, pieces[i].armor);
					}
					changed = true;
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
		return changed;
	}

	std::vector<std::size_t> WornOf(const State& a_state, const std::vector<std::size_t>& a_pieces, const std::vector<std::uint32_t>& a_excluded)
	{
		return OutfitRows::WornPieces(a_pieces, a_excluded, [&a_state](std::size_t a_index) { return a_state.request.pieces[a_index].armor; });
	}

	std::vector<std::uint32_t> ExcludedAmong(const State& a_state, const std::vector<std::size_t>& a_pieces, std::vector<std::uint32_t> a_excluded)
	{
		const auto& pieces = a_state.request.pieces;
		const auto  names = [&](std::uint32_t a_armor) {
			return std::ranges::any_of(a_pieces, [&](std::size_t a_index) { return pieces[a_index].armor == a_armor; });
		};
		std::erase_if(a_excluded, [&](std::uint32_t a_armor) { return !names(a_armor); });
		const bool every = std::ranges::all_of(a_pieces, [&](std::size_t a_index) { return std::ranges::find(a_excluded, pieces[a_index].armor) != a_excluded.end(); });
		if (every) {
			a_excluded.clear();
		}
		return a_excluded;
	}

	// The outfit's pieces as the library had them when the window opened, their list ending by
	// screen Y `a_bottom` (DrawPieceNames).
	void DrawOutfitPieces(const State& a_state, const CaptureWindow::ExistingOutfit& a_outfit, float a_bottom = FLT_MAX)
	{
		Theme::CountText(PieceCountText(a_outfit.pieces.size(), a_outfit.unloadedPieces).c_str());
		DrawPieceNames(a_state, a_outfit.pieces, a_bottom);
	}

	// Right column of the Load page: the selected outfit's name and pieces, read
	// only; `a_hint` while none is selected.
	void DrawOutfitDetails(const State& a_state, int a_selected, const char* a_hint)
	{
		if (!DrawSelectedName(a_state, a_selected, a_hint)) {
			return;
		}
		const auto& outfit = OutfitAt(a_state, a_selected);
		Theme::AfterFieldGap();
		DrawOutfitPieces(a_state, outfit);
	}

	// The name with no label, as the Wardrobe page's field; the situations' label with the
	// situation star, as the Situation List's headings have it.
	bool DrawOutfitSummary(const State& a_state, int a_index, const std::string& a_situations, float a_bottom, std::vector<std::uint32_t>* a_excluded)
	{
		DrawSelectedName(a_state, a_index, "");
		const auto& outfit = OutfitAt(a_state, a_index);
		Theme::LabelText(a_state.situationsLabel.c_str(), true, true);
		ImGui::TextWrapped("%s", a_situations.c_str());
		Theme::AfterFieldGap();
		if (!a_excluded) {
			DrawOutfitPieces(a_state, outfit, a_bottom);
			return false;
		}
		Theme::CountText(PieceCountText(outfit.pieces.size(), outfit.unloadedPieces).c_str());
		return DrawPieceChoices(a_state, outfit.pieces, *a_excluded, a_bottom);
	}

	// --- Load page ------------------------------------------------------------

	enum class LoadAction
	{
		kNone,
		kLoad,
		kBack
	};

	// Load / Back centred below a gap. a_escapeTaken: the browser used this
	// frame's Escape. An outfit with no loaded piece would tick nothing.
	// The page lists chests in place of outfits (State::loadSource).
	bool LoadsChests(const State& a_state)
	{
		return a_state.loadSource == LoadSource::kChests;
	}

	// Load (Add from the chest editor) and Back. Load is greyed while nothing is selected or
	// none of the selection's items is loaded.
	LoadAction DrawLoadButtons(const State& a_state, bool a_escapeTaken)
	{
		const char* label = a_state.loadTarget == LoadTarget::kChest ? "Add" : "Load";
		const char* noLoad = LoadsChests(a_state) ? kNeedChest : kNeedOutfit;
		if (LoadsChests(a_state) && a_state.previewedChest >= 0) {
			noLoad = ChestAt(a_state, a_state.previewedChest).pieces.empty() ? kNoLoadedPiece : nullptr;
		} else if (!LoadsChests(a_state) && a_state.previewedOutfit >= 0) {
			noLoad = OutfitAt(a_state, a_state.previewedOutfit).pieces.empty() ? kNoLoadedPiece : nullptr;
		}
		BeginButtonRow({ label, "Back" });
		const bool load = ReasonButton(label, noLoad);
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
		a_state.inventorySources = { { .id = outfit.id, .name = outfit.name, .pieces = outfit.pieces } };
		SetInventoryShown(a_state);
	}

	// Lists the selected chest's items on the Inventory page and ticks none: the ticks stay, so
	// the listed pieces nobody carries that are ticked stay listed too (DrawPieceRows' contract)
	// and the unticked ones go. The heading names what the list came from.
	void LoadChest(State& a_state)
	{
		const auto& chest = ChestAt(a_state, a_state.previewedChest);
		auto&       sources = a_state.inventorySources;
		std::erase_if(sources, [&chest](const ShownSource& a_source) { return a_source.chest && a_source.id == chest.id; });  // loaded again, whole
		for (auto& source : sources) {
			std::erase_if(source.pieces, [&a_state](std::size_t a_piece) { return !a_state.pieceTicked[a_piece]; });
			source.whole = false;
		}
		std::erase_if(sources, [](const ShownSource& a_source) { return a_source.pieces.empty(); });
		sources.push_back({ .chest = true, .id = chest.id, .name = chest.name, .pieces = chest.pieces });
		SetInventoryShown(a_state);
	}

	// A load opened from the Edit page adds to that page's ticks or list (Edit page section).
	std::vector<std::size_t> EditPreview(const State& a_state);
	void                     AddToEdit(State& a_state);
	void                     AddChestToEdit(State& a_state);

	// The Load page's preview, with outfits listed: the selected outfit, or with none the ticks
	// it goes back to; opened from the Edit page, that page's ticks with the selected outfit
	// added. Chests, and the chest editor's loads, change nothing on the preview.
	void PreviewLoad(const State& a_state)
	{
		if (LoadsChests(a_state) || a_state.loadTarget == LoadTarget::kChest) {
			return;
		}
		if (a_state.loadTarget == LoadTarget::kEdit) {
			QueuePreview(a_state, EditPreview(a_state));
		} else {
			PreviewOutfitOrTicks(a_state, a_state.previewedOutfit);
		}
	}

	// What Load does with the selection, by the page it returns to (State::loadTarget) and what
	// the page lists.
	void LoadSelection(State& a_state)
	{
		switch (a_state.loadTarget) {
		case LoadTarget::kInventory:
			if (LoadsChests(a_state)) {
				LoadChest(a_state);
			} else {
				LoadOutfit(a_state);
			}
			break;
		case LoadTarget::kEdit:
			if (LoadsChests(a_state)) {
				AddChestToEdit(a_state);
			} else {
				AddToEdit(a_state);
			}
			break;
		case LoadTarget::kChest:
			AddToChest(a_state, LoadsChests(a_state) ? ChestAt(a_state, a_state.previewedChest).pieces : OutfitAt(a_state, a_state.previewedOutfit).pieces);
			break;
		}
	}

	// Selecting an outfit shows it on the preview; a second click on it, or Back,
	// shows the ticks again. Nothing here changes the ticks until Load. Opened from the Edit
	// page, Load adds to that page's ticks and both go back there; from the chest editor, Add
	// puts the selection's items in the chest. With chests listed (Add from Chest...),
	// selecting one shows its items in the right column, and Load lists them.
	void DrawLoadPage(State& a_state)
	{
		const bool      chests = LoadsChests(a_state);
		BrowserControls controls;
		int             clicked = -1;
		if (BeginColumns("outfits")) {
			ImGui::TableNextColumn();
			if (chests) {
				// The chest being edited is left out of its own Add from Chest... list.
				const int except = a_state.loadTarget == LoadTarget::kChest ? a_state.chest.chest : -1;
				clicked = DrawChestList(a_state, a_state.previewedChest, except, 0.0f);
			} else {
				controls = DrawBrowserControls(a_state, a_state.loadBrowser);
				clicked = DrawOutfitList(a_state, a_state.loadBrowser, a_state.previewedOutfit, controls.height);
			}
			ImGui::TableNextColumn();
			ResetCellBaseline();
			if (chests) {
				DrawChestDetails(a_state, a_state.previewedChest, "Select a chest");
			} else {
				DrawOutfitDetails(a_state, a_state.previewedOutfit, "Select an outfit to load");
			}
			EndColumns();
		}
		if (clicked >= 0 && chests) {
			a_state.previewedChest = clicked == a_state.previewedChest ? -1 : clicked;
		} else if (clicked >= 0) {
			a_state.previewedOutfit = clicked == a_state.previewedOutfit ? -1 : clicked;
			PreviewLoad(a_state);
		}

		const auto action = DrawLoadButtons(a_state, controls.escapeTaken);
		EndButtonRow();
		Page from = Page::kCapture;
		if (a_state.loadTarget == LoadTarget::kEdit) {
			from = Page::kEdit;
		} else if (a_state.loadTarget == LoadTarget::kChest) {
			from = Page::kChestEdit;
		}
		switch (action) {
		case LoadAction::kLoad:
			QueueMenuSound(kOkSound);
			LoadSelection(a_state);
			GoToPage(a_state, from);
			break;
		case LoadAction::kBack:
			QueueMenuSound(kCancelSound);
			if (!chests && a_state.previewedOutfit >= 0) {
				a_state.previewedOutfit = -1;
				PreviewLoad(a_state);  // the ticks it goes back to
			}
			GoToPage(a_state, from);
			break;
		case LoadAction::kNone:
			break;
		}
	}

	// --- Wardrobe page ------------------------------------------------------
	//
	// The tab beside the Inventory page. The edits belong to the selected outfit and stay
	// until another is selected or the window closes; the preview shows them, or the
	// Inventory page's ticks while none is selected. The actor's rows change only by Apply,
	// which dresses them in the outfit now (DrawManageApplyPopup).

	// The selected outfit's library id. Contract: an outfit is selected.
	std::uint32_t ManagedOutfitId(const State& a_state)
	{
		return OutfitAt(a_state, a_state.manage.outfit).id;
	}

	std::string ApplyTooltip(const State& a_state);

	// The row whose left-off items the Wardrobe page's piece list starts from for library outfit
	// `a_outfit`: the one the actor wears it for now, which Apply gives it again
	// (DirectApplyRow), else the Default.
	std::size_t SelectionRow(const State& a_state, std::uint32_t a_outfit)
	{
		return a_state.request.wornOutfit == a_outfit ? DirectApplyRow(a_state) : 0;
	}

	// The items that row has `a_outfit` leave off, among `a_pieces` (ExcludedAmong).
	std::vector<std::uint32_t> StoredExcluded(const State& a_state, std::uint32_t a_outfit, const std::vector<std::size_t>& a_pieces)
	{
		const auto& assigned = a_state.request.assigned;
		const auto  row = SelectionRow(a_state, a_outfit);
		return row < assigned.size() ? ExcludedAmong(a_state, a_pieces, OutfitRows::ExcludedOf(assigned[row].row, a_outfit)) : std::vector<std::uint32_t>{};
	}

	// The selected outfit's pieces as edited, less those unticked: what the preview shows.
	std::vector<std::size_t> ManagedWorn(const State& a_state)
	{
		return WornOf(a_state, a_state.manage.pieces, a_state.manage.excluded);
	}

	void FillManageForm(State& a_state, int a_index)
	{
		auto& form = a_state.manage;
		form.outfit = a_index;
		a_state.error.clear();
		if (a_index < 0) {
			form.pieces.clear();
			form.excluded.clear();
			a_state.manageApplyTip.clear();
			return;
		}
		const auto& outfit = OutfitAt(a_state, a_index);
		form.icon = outfit.icon;
		form.name.fill('\0');
		std::strncpy(form.name.data(), outfit.name.c_str(), kNameCapacity - 1);
		form.pieces = outfit.pieces;
		form.excluded = StoredExcluded(a_state, outfit.id, form.pieces);
		a_state.manageApplyTip = ApplyTooltip(a_state);
	}

	// Starts the edits of outfit `a_index` (-1 for none) and shows it on the preview, or
	// the Inventory page's ticks with none selected.
	void SelectManaged(State& a_state, int a_index)
	{
		FillManageForm(a_state, a_index);
		QueuePreview(a_state, a_index >= 0 ? ManagedWorn(a_state) : TickedPieces(a_state));
	}

	// The selected outfit's edits differ from the library: its icon, trimmed name or loaded pieces.
	bool HasLibraryEdits(const State& a_state)
	{
		const auto& form = a_state.manage;
		const auto& outfit = OutfitAt(a_state, form.outfit);
		return form.icon != outfit.icon || Trimmed(form.name.data()) != outfit.name || form.pieces != outfit.pieces;
	}

	// Anything Update would save: an outfit is selected and has library edits.
	bool HasManageEdits(const State& a_state)
	{
		return a_state.manage.outfit >= 0 && HasLibraryEdits(a_state);
	}

	// The line every unsaved-edits popup shows; "" with no outfit selected.
	std::string UnsavedText(const State& a_state)
	{
		return a_state.manage.outfit >= 0 ? std::format("You have unsaved changes for \"{}\".", OutfitAt(a_state, a_state.manage.outfit).name) : std::string{};
	}

	enum class ManageFormClick
	{
		kNone,
		kIcon,  // the icon field
		kEdit
	};

	// Right column: the selected outfit's icon and name as edited, then its pieces as
	// edited under a count line ending in Edit, ticked as Apply would dress the actor in them
	// (a tick puts the preview right). Returns what was clicked; the page opens the icon
	// picker in its own ID scope.
	ManageFormClick DrawManageForm(State& a_state)
	{
		auto& form = a_state.manage;
		if (form.outfit < 0) {
			Theme::LabelText("Select an outfit to edit");
			return ManageFormClick::kNone;
		}
		bool icon = false;
		DrawNameField(a_state, form.name, &form.icon, &icon);
		Theme::AfterFieldGap();
		Theme::CountText(PieceCountText(form.pieces.size(), OutfitAt(a_state, form.outfit).unloadedPieces).c_str());
		const bool edit = Theme::LabelLink("Edit");
		if (DrawPieceChoices(a_state, form.pieces, form.excluded)) {
			QueuePreview(a_state, ManagedWorn(a_state));
		}
		if (icon) {
			return ManageFormClick::kIcon;
		}
		return edit ? ManageFormClick::kEdit : ManageFormClick::kNone;
	}

	enum class ManageAction
	{
		kNone,
		kUpdate,
		kRemove,
		kApply,
		kCancel
	};

	// Apply to #{Name} changes what the actor wears now (docs/outfit-management-ui.md §4): the
	// outfit goes to one row, the situation outfit they wear when the user agrees, else the
	// Default (row 0).

	// The row of the situation the actor wears a library outfit for now (Request::wornFor), or
	// nullopt: they wear the Default, nothing of Livery's, or a situation removed since.
	std::optional<std::size_t> WornSituationRow(const State& a_state)
	{
		const auto& request = a_state.request;
		if (request.wornOutfit == 0 || request.wornFor == Situations::Situation::kDefault) {
			return std::nullopt;
		}
		const auto found = std::ranges::find(request.situations, request.wornFor, &CaptureWindow::SituationInfo::situation);
		if (found == request.situations.end()) {
			return std::nullopt;
		}
		return static_cast<std::size_t>(found - request.situations.begin());
	}

	std::optional<std::size_t> OfferedRow(const State& a_state, std::uint32_t a_outfit)
	{
		const auto row = WornSituationRow(a_state);
		return row && a_state.request.wornOutfit != a_outfit ? row : std::nullopt;
	}

	std::size_t DirectApplyRow(const State& a_state)
	{
		return WornSituationRow(a_state).value_or(0);
	}

	bool ApplyAsksFirst(const State& a_state, std::uint32_t a_outfit)
	{
		return OfferedRow(a_state, a_outfit) || (DirectApplyRow(a_state) == 0 && IsTaken(a_state, 0, a_outfit));
	}

	// Apply's tooltip: what it dresses the actor for. Contract: an outfit is selected.
	std::string ApplyTooltip(const State& a_state)
	{
		const auto& name = a_state.request.actorName;
		if (const auto offered = OfferedRow(a_state, ManagedOutfitId(a_state))) {
			return std::format("Dress {} in this outfit as their {} outfit, or make it their Default.", name, RowName(a_state, *offered));
		}
		const auto row = DirectApplyRow(a_state);
		return row == 0 ? std::format("Dress {} in this outfit as their Default.", name) : std::format("Dress {} in this outfit for {}.", name, RowName(a_state, row));
	}

	// Whether Apply would change nothing: no library edits, and the actor wears the selected
	// outfit now, every piece Livery put on still on them (a piece taken back in the trade menu
	// is not: Apply puts a copy on) and nothing else worn (Apply takes it off), for the Default
	// or a situation not removed, the row Apply would give it again, with the items that row
	// leaves off unticked.
	// Contract: an outfit is selected.
	bool WearsManagedAsIs(const State& a_state)
	{
		const auto& request = a_state.request;
		const auto  id = ManagedOutfitId(a_state);
		if (HasLibraryEdits(a_state) || request.wornOutfit != id || request.piecesNotWorn > 0 || request.extraWorn > 0) {
			return false;
		}
		if (a_state.manage.excluded != StoredExcluded(a_state, id, a_state.manage.pieces)) {
			return false;
		}
		return request.wornFor == Situations::Situation::kDefault || WornSituationRow(a_state).has_value();
	}

	// Why Apply to #{Name} is greyed, or nullptr. It dresses the actor in the outfit as
	// edited: unsaved library edits are saved first, so they need a name.
	const char* ApplyReason(const State& a_state)
	{
		const auto& form = a_state.manage;
		if (form.outfit < 0) {
			return kNeedOutfit;
		}
		if (form.pieces.empty()) {
			return kNoLoadedPiece;
		}
		if (HasLibraryEdits(a_state) && Trimmed(form.name.data()).empty()) {
			return kNeedName;
		}
		return WearsManagedAsIs(a_state) ? kWornAlready : nullptr;
	}

	// Why Update is greyed, or nullptr: it needs unsaved edits and a name.
	const char* UpdateReason(const State& a_state)
	{
		if (!HasManageEdits(a_state)) {
			return a_state.manage.outfit < 0 ? kNeedOutfit : kNoChanges;
		}
		return Trimmed(a_state.manage.name.data()).empty() ? kNeedName : nullptr;
	}

	// Update / Apply to #{Name} / Exit centred below a gap, then Remove at the row's right
	// end, a link as Restore is. Update needs Request::onManageUpdate (UpdateReason);
	// Remove needs Request::onManageRemove and a selected outfit; Apply needs
	// Request::onManageApply (ApplyReason), and says what it dresses the actor for on hover.
	// a_escapeTaken: the browser used this frame's Escape, which is otherwise Exit.
	ManageAction DrawManageButtons(const State& a_state, bool a_escapeTaken)
	{
		const char* noUpdate = UpdateReason(a_state);
		const char* apply = a_state.manageApplyLabel.c_str();
		BeginButtonRow({ "Update", apply, "Exit" });
		ImGui::BeginDisabled(!a_state.request.onManageUpdate);
		const bool update = ReasonButton("Update", noUpdate);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!a_state.request.onManageApply);
		const bool applied = ReasonButton(apply, ApplyReason(a_state), a_state.manageApplyTip.c_str());
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		const bool cancel = Theme::Button("Exit") || escape;

		const bool noOutfit = a_state.manage.outfit < 0;
		ImGui::BeginDisabled(noOutfit || !a_state.request.onManageRemove);
		const bool remove = Theme::LineLink("Remove");
		const bool removeHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		// After EndDisabled, so the tooltip is not drawn faded.
		if (noOutfit && removeHovered) {
			Theme::Tooltip(kNeedOutfit);
		}

		if (update) {
			return ManageAction::kUpdate;
		}
		if (remove) {
			return ManageAction::kRemove;
		}
		if (applied) {
			return ManageAction::kApply;
		}
		return cancel ? ManageAction::kCancel : ManageAction::kNone;
	}

	enum class UnsavedChoice
	{
		kNone,
		kFirst,
		kSecond
	};

	void DrawLibraryChangeLines(const State& a_state);

	// A popup with UnsavedText, the library changes, and buttons `a_first` and `a_second`;
	// Escape is the second.
	UnsavedChoice DrawUnsavedPopup(const State& a_state, const char* a_id, const char* a_first, const char* a_second)
	{
		if (!BeginModal(a_id)) {
			return UnsavedChoice::kNone;
		}
		if (a_state.manage.outfit < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return UnsavedChoice::kNone;
		}
		ImGui::TextUnformatted(UnsavedText(a_state).c_str());
		DrawLibraryChangeLines(a_state);
		BeginButtonRow({ a_first, a_second });
		const bool first = Theme::Button(a_first);
		ImGui::SameLine();
		const bool second = Theme::Button(a_second) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (first || second) {
			QueueMenuSound(first ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return first ? UnsavedChoice::kFirst : (second ? UnsavedChoice::kSecond : UnsavedChoice::kNone);
	}

	void OpenEditPage(State& a_state);

	// What the library edits change, one line each, as the Save page's Update tab lists it:
	// the pieces, a rename, a new icon, the unloaded pieces kept.
	void DrawLibraryChangeLines(const State& a_state)
	{
		const auto& form = a_state.manage;
		const auto& outfit = OutfitAt(a_state, form.outfit);
		if (form.pieces != outfit.pieces) {
			ImGui::TextUnformatted(std::format("Its {} item(s) become the {} edited item(s).", outfit.pieces.size(), form.pieces.size()).c_str());
		}
		if (const auto name = Trimmed(form.name.data()); name != outfit.name) {
			ImGui::TextUnformatted(std::format("It is renamed \"{}\".", name).c_str());
		}
		if (form.icon != outfit.icon) {
			ImGui::TextUnformatted("Its icon changes.");
		}
		if (outfit.unloadedPieces) {
			ImGui::TextDisabled("%s", std::format("{} item(s) from unloaded plugins stay in it.", outfit.unloadedPieces).c_str());
		}
	}

	// Update's popup: `Save your changes to "X"?`, the library changes, then Confirm / Cancel.
	// Returns true on Confirm. Cancel also calls off the leave the unsaved-changes alert's
	// Save Changes opened it for.
	bool DrawManageConfirmPopup(State& a_state)
	{
		if (!BeginModal(kManageConfirmPopup)) {
			return false;
		}
		const auto& form = a_state.manage;
		if (form.outfit < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		ImGui::TextUnformatted(std::format("Save your changes to \"{}\"?", OutfitAt(a_state, form.outfit).name).c_str());
		DrawLibraryChangeLines(a_state);
		BeginButtonRow({ "Confirm", "Cancel" });
		const bool confirm = Theme::Button("Confirm");
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (confirm || cancel) {
			QueueMenuSound(confirm ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		if (cancel) {
			a_state.leave.reset();
		}
		ImGui::EndPopup();
		return confirm;
	}

	// Whether Apply needs the popup: library edits (saved first), or ApplyAsksFirst.
	// Contract: an outfit is selected.
	bool ApplyNeedsConfirm(const State& a_state)
	{
		return HasLibraryEdits(a_state) || ApplyAsksFirst(a_state, ManagedOutfitId(a_state));
	}

	// the Wardrobe page's selected outfit as its Apply popup names it; unsaved library edits are
	// saved first. Contract: an outfit is selected.
	ApplySubject ManagedSubject(const State& a_state)
	{
		const auto& outfit = OutfitAt(a_state, a_state.manage.outfit);
		const bool  edits = HasLibraryEdits(a_state);
		return { .name = outfit.name,
			.id = outfit.id,
			.applyLabel = edits ? "Update and Apply" : "Apply",
			.saveFirst = edits ? std::format("Save your changes to \"{}\"", outfit.name) : std::string{},
			.saveFirstHelp = edits ? " Your changes are saved first." : "",
			.drawSaveLines = edits ? &DrawLibraryChangeLines : nullptr };
	}

	// The question of Apply's popup for offered row `a_row`: random off `Make "A" their new
	// Town outfit?` ("B" leaves the row), random on `Add "A" to them and wear it now?`, or
	// with "A" on that list already `Wear "A" from them now?`; with something saved first
	// `Save your changes to "A", then make it ...?` (`Create "A", then ...?`).
	std::string OfferQuestion(const State& a_state, const ApplySubject& a_subject, std::size_t a_row)
	{
		const auto& row = a_state.request.assigned[a_row].row;
		const bool  first = !a_subject.saveFirst.empty();
		const auto  it = first ? std::string("it") : std::format("\"{}\"", a_subject.name);
		std::string action;
		if (!row.random) {
			action = std::format("make {} their new {} outfit", it, RowName(a_state, a_row));
		} else if (OutfitRows::Holds(row, a_subject.id)) {
			action = std::format("wear {} from them now", it);
		} else {
			action = std::format("add {} to them and wear it now", it);
		}
		if (first) {
			return std::format("{}, then {}?", a_subject.saveFirst, action);
		}
		action.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(action.front())));
		return action + "?";
	}

	// What Apply as Default does, for its "(?)": the outfit becomes the Default and row
	// `a_row` keeps its outfit, so the new Default shows only once no situation outfit applies.
	std::string AsDefaultHelp(const State& a_state, const ApplySubject& a_subject, std::size_t a_row)
	{
		const auto& name = a_state.request.actorName;
		const auto* situation = RowName(a_state, a_row);
		auto        help = std::format("\"{}\" becomes {}'s Default instead, and their {} outfit stays. The new Default shows only when {} ends and no other situational outfit applies.",
			a_subject.name, name, situation, situation);
		if (IsTaken(a_state, 0, a_subject.id)) {
			help += std::format(" It replaces the current Default, {}.", HeldOutfitName(a_state, 0));
		}
		return help + a_subject.saveFirstHelp;
	}

	// Apply's popup when it offers row `a_row`, the situation the actor wears another outfit
	// for now: `Lydia is wearing their Town outfit, "B".` (`one of their Town outfits` with
	// random on), the question (OfferQuestion), the lines of what is saved first, then Yes, Apply as
	// Default (its "(?)" says what that does, AsDefaultHelp) and Cancel. Returns `a_row` on
	// Yes, the Default (0) on Apply as Default.
	std::optional<std::size_t> DrawOfferPopupBody(const State& a_state, const ApplySubject& a_subject, std::size_t a_row)
	{
		constexpr const char* kAsDefault = "Apply as Default";
		constexpr const char* kAsDefaultWidth = "Apply as Default (?)";  // what the button's width is measured on (Theme::HelpButton)

		const auto& request = a_state.request;
		const auto* situation = RowName(a_state, a_row);
		const auto  wearing = request.assigned[a_row].row.random ? std::format("{} is wearing one of their {} outfits, \"{}\".", request.actorName, situation, request.outfitName) :
		                                                           std::format("{} is wearing their {} outfit, \"{}\".", request.actorName, situation, request.outfitName);
		ImGui::TextUnformatted(wearing.c_str());
		ImGui::TextUnformatted(OfferQuestion(a_state, a_subject, a_row).c_str());
		if (a_subject.drawSaveLines) {
			a_subject.drawSaveLines(a_state);
		}
		BeginButtonRow({ "Yes", kAsDefaultWidth, "Cancel" });
		const bool yes = Theme::Button("Yes");
		ImGui::SameLine();
		bool       helpHovered = false;
		const bool asDefault = Theme::HelpButton(kAsDefault, &helpHovered);
		if (helpHovered) {
			DrawHelpTooltip(AsDefaultHelp(a_state, a_subject, a_row));
		}
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (yes || asDefault || cancel) {
			QueueMenuSound(cancel ? kCancelSound : kOkSound);
			ImGui::CloseCurrentPopup();
		}
		if (yes) {
			return a_row;
		}
		return asDefault ? std::optional<std::size_t>(0) : std::nullopt;
	}

	// Apply's popup with no row offered: `Apply "A" as #{Name}'s Default?` (`... to #{Name} for
	// Town?` when they wear it for that situation now), or with something saved first `Save
	// your changes to "A", then apply it ...?` (`Create "A", then ...?`) and its lines; the
	// Default it replaces. Then `a_subject.applyLabel` and Cancel. Returns the row
	// (DirectApplyRow) on the first.
	std::optional<std::size_t> DrawDirectApplyPopupBody(const State& a_state, const ApplySubject& a_subject)
	{
		const auto& name = a_state.request.actorName;
		const auto  row = DirectApplyRow(a_state);
		const auto  target = row == 0 ? std::format("as {}'s Default", name) : std::format("to {} for {}", name, RowName(a_state, row));
		const auto  question = a_subject.saveFirst.empty() ? std::format("Apply \"{}\" {}?", a_subject.name, target) :
		                                                     std::format("{}, then apply it {}?", a_subject.saveFirst, target);
		ImGui::TextUnformatted(question.c_str());
		if (a_subject.drawSaveLines) {
			a_subject.drawSaveLines(a_state);
		}
		if (row == 0 && IsTaken(a_state, 0, a_subject.id)) {
			ImGui::TextUnformatted(std::format("The current Default, {}, will be replaced.", HeldOutfitName(a_state, 0)).c_str());
		}
		const char* applyLabel = a_subject.applyLabel.c_str();
		BeginButtonRow({ applyLabel, "Cancel" });
		const bool apply = Theme::Button(applyLabel);
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (apply || cancel) {
			QueueMenuSound(apply ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		return apply ? std::optional<std::size_t>(row) : std::nullopt;
	}

	std::optional<std::size_t> DrawApplyPopupBody(const State& a_state, const ApplySubject& a_subject)
	{
		const auto offered = OfferedRow(a_state, a_subject.id);
		return offered ? DrawOfferPopupBody(a_state, a_subject, *offered) : DrawDirectApplyPopupBody(a_state, a_subject);
	}

	// Apply's popup (DrawApplyPopupBody). Returns the row the user applies the outfit to, or
	// nullopt.
	std::optional<std::size_t> DrawManageApplyPopup(const State& a_state)
	{
		if (!BeginModal(kManageApplyPopup)) {
			return std::nullopt;
		}
		if (a_state.manage.outfit < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return std::nullopt;
		}
		const auto row = DrawApplyPopupBody(a_state, ManagedSubject(a_state));
		ImGui::EndPopup();
		return row;
	}

	// Hands the edits to onManageUpdate, which writes the library, with `a_applyRow` (Update
	// and Apply) dresses the actor for that row as Apply does, and ends with SetOutfits, or
	// ends with SetError; until then the page is greyed. Update alone changes no row.
	// Contract: the handler writes nothing for a refused name or a failed write, and then
	// assigns nothing (CLAUDE.md §2.1).
	void RequestManageUpdate(State& a_state, std::optional<std::size_t> a_applyRow)
	{
		const auto& form = a_state.manage;
		a_state.status = Status::kSaving;
		a_state.error.clear();
		CaptureWindow::SaveRequest edit{ .outfit = ManagedOutfitId(a_state),
			.name = Trimmed(form.name.data()),
			.pieces = form.pieces,
			.icon = form.icon };
		edit.applyRow = a_applyRow;
		if (a_applyRow) {
			edit.excluded = form.excluded;
		}
		const bool apply = a_applyRow.has_value();
		auto       onUpdate = a_state.request.onManageUpdate;
		SKSE::GetTaskInterface()->AddTask([onUpdate, edit = std::move(edit), apply] { onUpdate(edit, apply); });
	}

	// Names the outfit and who has it among Livery's assignments in this save, then Remove /
	// Cancel. Returns true on Remove.
	bool DrawRemovePopup(const State& a_state)
	{
		if (!BeginModal(kRemovePopup)) {
			return false;
		}
		const auto& form = a_state.manage;
		if (form.outfit < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		const auto& outfit = OutfitAt(a_state, form.outfit);
		ImGui::TextUnformatted(std::format("Remove \"{}\" from the wardrobe?", outfit.name).c_str());
		if (!outfit.users.empty()) {
			ImGui::TextUnformatted("It is used by:");
			for (const auto& user : outfit.users) {
				ImGui::BulletText("%s", user.c_str());
			}
			ImGui::TextDisabled("Their situations without it fall back to the default; a default from it keeps its items.");
		}
		BeginButtonRow({ "Remove", "Cancel" });
		const bool remove = Theme::Button("Remove");
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (remove || cancel) {
			QueueMenuSound(remove ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return remove;
	}

	// The items ticked on the Wardrobe page that the player would not get (FadesNotCarried).
	std::vector<std::size_t> ManagedNotCarried(const State& a_state)
	{
		std::vector<std::size_t> missing;
		for (const auto i : ManagedWorn(a_state)) {
			if (FadesNotCarried(a_state, i)) {
				missing.push_back(i);
			}
		}
		return missing;
	}

	// Lists the ticked items the player does not carry, then OK / Back. Returns true on OK;
	// OK is greyed while they carry none of the ticked items, as nothing could go on.
	// Contract: asks before Apply's popup and before any library write, so Back changes nothing.
	bool DrawNotCarriedPopup(const State& a_state)
	{
		if (!BeginModal(kNotCarriedPopup)) {
			return false;
		}
		const auto& form = a_state.manage;
		const auto  missing = form.outfit < 0 ? std::vector<std::size_t>{} : ManagedNotCarried(a_state);
		if (missing.empty()) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		ImGui::TextUnformatted(std::format("You don't carry these items from \"{}\":", OutfitAt(a_state, form.outfit).name).c_str());
		for (const auto i : missing) {
			ImGui::BulletText("%s", a_state.request.pieces[i].name.c_str());
		}
		const bool carriesNone = missing.size() == ManagedWorn(a_state).size();
		if (!carriesNone) {
			ImGui::TextUnformatted("Only the items you carry go on.");
		}
		BeginButtonRow({ "OK", "Back" });
		const bool ok = ReasonButton("OK", carriesNone ? "You carry none of these items." : nullptr);
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (ok || back) {
			QueueMenuSound(ok ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return ok;
	}

	// Hands the outfit to onManageRemove; until the handler ends with SetOutfits or SetError
	// the page is greyed.
	// Contract: the handler changes no assignment for a refused or failed write (CLAUDE.md §2.1).
	void RequestManageRemove(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onRemove = a_state.request.onManageRemove;
		SKSE::GetTaskInterface()->AddTask([onRemove, outfit = ManagedOutfitId(a_state)] { onRemove(outfit); });
	}

	// Apply with no library edits: hands the outfit, row `a_row` and the unticked items to
	// onManageApply, which dresses the actor and ends with SetOutfits, or ends with SetError;
	// until then the page is greyed. With library edits RequestManageUpdate saves them first.
	void RequestManageApply(State& a_state, std::size_t a_row)
	{
		if (HasLibraryEdits(a_state)) {
			RequestManageUpdate(a_state, a_row);
			return;
		}
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onApply = a_state.request.onManageApply;
		SKSE::GetTaskInterface()->AddTask(
			[onApply, id = ManagedOutfitId(a_state), a_row, excluded = a_state.manage.excluded] { onApply(id, a_row, excluded); });
	}

	// Apply past the items-not-carried alert: asks when it has anything to say
	// (ApplyNeedsConfirm), else dresses at once.
	void ContinueManageApply(State& a_state)
	{
		if (ApplyNeedsConfirm(a_state)) {
			ImGui::OpenPopup(kManageApplyPopup);
		} else {
			RequestManageApply(a_state, DirectApplyRow(a_state));
		}
	}

	// Choose icon: the icons whose PNG is loaded, in OutfitIcons' order, the chosen one marked;
	// under them the colours, the chosen one marked; then OK / Cancel. A click on an icon or a
	// colour chooses it; while the mouse is over a colour every icon shows in it. OK hands the
	// choice back, Cancel (or Escape) nothing. Nothing is written here.
	std::optional<OutfitIcons::Icon> DrawIconPopup(State& a_state, bool a_keepOpen)
	{
		constexpr int   kColumns = 8;                // icons per row; to be tuned in game
		constexpr float kTileGap = 2.0f;             // pixels at 1080p, between the icons and between the colours
		constexpr float kSwatchHeightRatio = 0.75f;  // of the frame height

		if (!BeginModal(kIconPopup)) {
			return std::nullopt;
		}
		if (!a_keepOpen) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return std::nullopt;
		}
		auto&       pick = a_state.iconPicker.pick;
		const float gap = kTileGap * Theme::Scale();
		const auto  shown = a_state.iconPicker.hoverColor.value_or(pick.rgb);
		int         drawn = 0;
		for (const auto key : OutfitIcons::kKeys) {
			if (!Theme::HasOutfitIcon(key)) {
				continue;
			}
			if (drawn % kColumns != 0) {
				ImGui::SameLine(0.0f, gap);
			} else if (drawn > 0) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + gap);
			}
			ImGui::PushID(drawn++);
			if (Theme::IconTile("##icon", { key, shown }, pick.key == key)) {
				QueueMenuSound(kFocusSound);
				pick.key = key;
			}
			ImGui::PopID();
		}

		const auto  colors = OutfitIcons::kColors.size();
		const float width = static_cast<float>(kColumns) * Theme::IconTileSide() + static_cast<float>(kColumns - 1) * gap;
		const float swatch = (width - static_cast<float>(colors - 1) * gap) / static_cast<float>(colors);
		Theme::SectionGap();
		std::optional<std::uint32_t> hovered;
		for (std::size_t i = 0; i < colors; ++i) {
			if (i > 0) {
				ImGui::SameLine(0.0f, gap);
			}
			const auto rgb = OutfitIcons::kColors[i];
			ImGui::PushID(static_cast<int>(i));
			if (Theme::ColorSwatch("##color", rgb, pick.rgb == rgb, swatch, ImGui::GetFrameHeight() * kSwatchHeightRatio)) {
				QueueMenuSound(kFocusSound);
				pick.rgb = rgb;
			}
			if (ImGui::IsItemHovered()) {
				hovered = rgb;
			}
			ImGui::PopID();
		}
		a_state.iconPicker.hoverColor = hovered;

		BeginButtonRow({ "OK", "Cancel" });
		const bool ok = Theme::Button("OK");
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (ok || cancel) {
			QueueMenuSound(ok ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return ok ? std::optional{ pick } : std::nullopt;
	}

	// The page's popups, each opened by the page in its own ID scope. Choose icon's OK makes
	// the choice the edited outfit's icon, an edit as a rename is. The unsaved-changes
	// alert's Save Changes saves through Update's popup, whose Confirm the leave follows
	// once the save took (Draw); Discard Changes starts the edits again from the library.
	void DrawManagePopups(State& a_state)
	{
		if (const auto icon = DrawIconPopup(a_state, a_state.manage.outfit >= 0)) {
			a_state.manage.icon = *icon;
		}
		if (DrawManageConfirmPopup(a_state)) {
			RequestManageUpdate(a_state, std::nullopt);
			a_state.leaveAfterSave = a_state.leave.has_value();
		}
		if (DrawNotCarriedPopup(a_state)) {
			ContinueManageApply(a_state);
		}
		if (const auto row = DrawManageApplyPopup(a_state)) {
			RequestManageApply(a_state, *row);
		}
		if (DrawRemovePopup(a_state)) {
			RequestManageRemove(a_state);
		}
		if (DrawUnsavedPopup(a_state, kDiscardPopup, "Discard changes", "Keep editing") == UnsavedChoice::kFirst) {
			SelectManaged(a_state, a_state.discardFor);
		}
		switch (DrawLeavePopup(a_state, UnsavedText(a_state), UpdateReason(a_state), DrawLibraryChangeLines)) {
		case LeaveChoice::kSave:
			ImGui::OpenPopup(kManageConfirmPopup);
			break;
		case LeaveChoice::kDiscard:
			FillManageForm(a_state, a_state.manage.outfit);
			Leave(a_state, *std::exchange(a_state.leave, std::nullopt));
			break;
		case LeaveChoice::kBack:
		case LeaveChoice::kNone:
			break;
		}
	}

	// Selecting an outfit starts its edits and shows it on the preview; a second click on
	// it deselects it. Either asks first while the selected one has unsaved edits. Apply first
	// lists the ticked items the player does not carry (DrawNotCarriedPopup), then asks when it
	// has anything to say (ApplyNeedsConfirm), else dresses at once.
	void DrawManagePage(State& a_state)
	{
		if (std::exchange(a_state.openLeave, false)) {
			ImGui::OpenPopup(kLeavePopup);
		}
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		BrowserControls controls;
		int             clicked = -1;
		auto            formClick = ManageFormClick::kNone;
		if (BeginColumns("manage")) {
			ImGui::TableNextColumn();
			controls = DrawBrowserControls(a_state, a_state.manageBrowser);
			clicked = DrawOutfitList(a_state, a_state.manageBrowser, a_state.manage.outfit, controls.height);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			formClick = DrawManageForm(a_state);
			EndColumns();
		}
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		const auto action = DrawManageButtons(a_state, controls.escapeTaken);
		EndButtonRow();
		ImGui::EndDisabled();

		if (clicked >= 0) {
			const int target = clicked == a_state.manage.outfit ? -1 : clicked;
			if (HasManageEdits(a_state)) {
				a_state.discardFor = target;
				ImGui::OpenPopup(kDiscardPopup);
			} else {
				SelectManaged(a_state, target);
			}
		}
		if (formClick == ManageFormClick::kEdit) {
			QueueMenuSound(kOkSound);
			OpenEditPage(a_state);
		} else if (formClick == ManageFormClick::kIcon) {
			QueueMenuSound(kOkSound);
			a_state.iconPicker = { .pick = a_state.manage.icon };
			ImGui::OpenPopup(kIconPopup);
		}
		if (action == ManageAction::kUpdate) {
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kManageConfirmPopup);
		} else if (action == ManageAction::kRemove) {
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kRemovePopup);
		} else if (action == ManageAction::kApply) {
			QueueMenuSound(kOkSound);
			if (!ManagedNotCarried(a_state).empty()) {
				ImGui::OpenPopup(kNotCarriedPopup);
			} else {
				ContinueManageApply(a_state);
			}
		} else if (action == ManageAction::kCancel) {
			RequestLeave(a_state, { .exit = true });
		}
		DrawManagePopups(a_state);
	}

	// --- Edit page ------------------------------------------------------------

	PieceList EditPieces(const State& a_state)
	{
		return { a_state.editTicked.get(), &a_state.editShown, &a_state.editShownHeading,
			a_state.editShownFromChest ? Theme::HeadingIcon::kChest : Theme::HeadingIcon::kBareForm };
	}

	// "\"A\", \"B\"": over the pieces nobody carries, the outfits they come
	// from (State::editOutfitNames).
	std::string EditShownHeading(const std::vector<std::string>& a_names)
	{
		std::string names;
		for (const auto& name : a_names) {
			names += std::format("{}\"{}\"", names.empty() ? "" : ", ", name);
		}
		return names;
	}

	// The page's ticks with the Load page's selected outfit added, when one is: its pieces
	// ticked, and a ticked piece sharing a body slot with one of them unticked, as ticking that
	// piece would untick it (UntickSharingSlots).
	std::vector<std::size_t> EditPreview(const State& a_state)
	{
		if (a_state.previewedOutfit < 0) {
			return Ticked(a_state, EditPieces(a_state));
		}
		const auto&   outfit = OutfitAt(a_state, a_state.previewedOutfit);
		const auto&   pieces = a_state.request.pieces;
		std::uint32_t slots = 0;  // the body slots the outfit's pieces take
		for (const auto piece : outfit.pieces) {
			slots |= pieces[piece].slots;
		}
		std::vector<std::size_t> ticked;
		for (std::size_t i = 0; i < pieces.size(); ++i) {
			if (HasPiece(outfit, i) || (a_state.editTicked[i] && (pieces[i].slots & slots) == 0)) {
				ticked.push_back(i);
			}
		}
		return ticked;
	}

	// The Load page's selected outfit added to the page's ticks (EditPreview), which the
	// preview already shows; its pieces stay listed, and the heading over those nobody carries
	// names it when it brings one. The Inventory page's ticks stay as they were.
	// Contract: an outfit is selected on the Load page.
	void AddToEdit(State& a_state)
	{
		const auto& outfit = OutfitAt(a_state, a_state.previewedOutfit);
		const auto  ticked = EditPreview(a_state);
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			a_state.editTicked[i] = std::ranges::binary_search(ticked, i);
		}
		const bool bringsUncarried = std::ranges::any_of(outfit.pieces, [&a_state](std::size_t a_piece) {
			return a_state.request.pieces[a_piece].group == CaptureWindow::PieceGroup::kNotCarried && !std::ranges::binary_search(a_state.editShown, a_piece);
		});

		std::vector<std::size_t> shown;
		std::ranges::set_union(a_state.editShown, outfit.pieces, std::back_inserter(shown));
		a_state.editShown = std::move(shown);
		auto& names = a_state.editOutfitNames;
		if (bringsUncarried && std::ranges::find(names, outfit.name) == names.end()) {
			names.push_back(outfit.name);
			a_state.editShownHeading = EditShownHeading(names);
			a_state.editShownFromChest = false;
		}
	}

	// The Load page's selected chest's items listed on the page, none of them ticked; the
	// heading over those nobody carries names the chest when it brings one.
	// Contract: a chest is selected on the Load page.
	void AddChestToEdit(State& a_state)
	{
		const auto&              chest = ChestAt(a_state, a_state.previewedChest);
		const bool               bringsUncarried = std::ranges::any_of(chest.pieces, [&a_state](std::size_t a_piece) {
			return a_state.request.pieces[a_piece].group == CaptureWindow::PieceGroup::kNotCarried && !std::ranges::binary_search(a_state.editShown, a_piece);
		});
		std::vector<std::size_t> shown;
		std::ranges::set_union(a_state.editShown, chest.pieces, std::back_inserter(shown));
		a_state.editShown = std::move(shown);
		auto& names = a_state.editOutfitNames;
		if (bringsUncarried && std::ranges::find(names, chest.name) == names.end()) {
			names.push_back(chest.name);
			a_state.editShownHeading = EditShownHeading(names);
			a_state.editShownFromChest = true;
		}
	}

	// Ticks exactly the edited pieces, which the preview already shows; they stay listed
	// while the page is open, those nobody carries under the outfit's heading. The
	// Inventory page's ticks stay as they were.
	void OpenEditPage(State& a_state)
	{
		const auto& form = a_state.manage;
		const auto& name = OutfitAt(a_state, form.outfit).name;
		for (std::size_t i = 0; i < a_state.request.pieces.size(); ++i) {
			a_state.editTicked[i] = std::ranges::binary_search(form.pieces, i);
		}
		a_state.editShown = form.pieces;
		a_state.editTitle = std::format("Edit \"{}\"", name);
		a_state.editOutfitNames = { name };
		a_state.editShownHeading = EditShownHeading(a_state.editOutfitNames);
		a_state.editShownFromChest = false;
		GoToPage(a_state, Page::kEdit);
	}

	// Use Ticked Pieces makes the ticks the edited pieces, which the preview already shows;
	// Back and Escape leave them, and the preview shows them again. Nothing is written.
	// Add from Wardrobe... opens the Load page, whose Load adds to the ticks.
	void DrawEditPage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		const auto list = EditPieces(a_state);
		DrawPieceRows(a_state, list);
		const auto links = DrawPieceCount(a_state, list, true);
		DrawPreviewToggle(a_state, list);
		BeginButtonRow({ "Use Ticked Items", "Back" });
		const auto ticked = Ticked(a_state, list);
		const bool use = ReasonButton("Use Ticked Items", ticked.empty() ? kNeedPiece : nullptr);
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
		EndButtonRow();
		ImGui::EndDisabled();

		if (links.wardrobe || links.chest) {
			QueueMenuSound(kOkSound);
			a_state.previewedOutfit = -1;
			a_state.previewedChest = -1;
			a_state.loadTarget = LoadTarget::kEdit;
			a_state.loadSource = links.chest ? LoadSource::kChests : LoadSource::kOutfits;
			GoToPage(a_state, Page::kLoad);
		} else if (use) {
			QueueMenuSound(kOkSound);
			a_state.manage.pieces = ticked;
			a_state.manage.excluded = ExcludedAmong(a_state, ticked, a_state.manage.excluded);
			if (!a_state.manage.excluded.empty()) {
				QueuePreview(a_state, ManagedWorn(a_state));  // else the preview shows the ticks already
			}
			GoToPage(a_state, Page::kManage);
		} else if (back) {
			QueueMenuSound(kCancelSound);
			QueuePreview(a_state, ManagedWorn(a_state));
			GoToPage(a_state, Page::kManage);
		}
	}
}
