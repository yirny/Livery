// SPDX-License-Identifier: GPL-3.0-or-later
//
// The capture window's outfit pages (see CaptureWindow.cpp for the window and the
// Inventory and Save pages, SituationPage.cpp for Manage Situations, CaptureWindowInternal.h
// for the state they share).
//
// FILE MAP
//   Outfit browser - Search (and Filter, hidden by kShowFilter), above an outfit page's
//                  list in its column, in the small font (OutfitBrowser.cpp narrows and
//                  orders it); each page has its own; Escape first clears the search
//   Outfit lists - the library outfit list with its Sort dropdown, the sort's headings (a
//                  click folds their outfits away) and each outfit's tags (off with Show
//                  Categories under it), the selected
//                  outfit's name (or a hint while none is) and its piece list, which
//                  the outfit pages and Manage Situations share (there each row has a grip
//                  and drags a copy of its outfit);
//                  on the Load and Select pages the selected outfit is on the preview,
//                  and a second click on it, or Back, shows the ticks again
//   Load page    - library outfits and the selected one's details; Load ticks its
//                  pieces and returns (greyed while none of them is loaded)
//   Select page  - the Update tab's outfit selection: library outfits and the selected
//                  one's details; Select fills the Update tab (ChooseOutfit), Cancel
//                  leaves it as it was
//   Manage Wardrobe - the tab beside the Inventory page: library outfits and the selected
//                  one's name, gender, categories, the actor's rows for it ("Situations for
//                  #{Name}") and pieces, edited in place and kept for one outfit at a time
//                  (Discard changes / Keep editing before another is selected); the edited
//                  outfit on the preview; Update lists the changes (Save changes?, a row
//                  whose worn outfit it replaces among them; Confirm / Cancel) and hands them to
//                  onManageUpdate, Remove names who has the outfit (Remove outfit?) and hands
//                  it to onManageRemove, Apply to #{Name} dresses the actor for the ticked
//                  rows (Apply outfit? first when it saves edits, takes over a row or replaces
//                  a Default) through onManageApply or onManageUpdate; Exit closes the window;
//                  Exit, Escape or another tab with unsaved edits shows the unsaved-changes
//                  alert, whose Save Changes goes through Update's popup
//   Edit page    - the edited pieces as the Inventory page's piece list with its own ticks
//                  and Live Preview; Use Ticked Pieces makes them the edited pieces, Back
//                  leaves them; nothing is written
#include "PCH.h"

#include "CaptureWindowInternal.h"

#include "OutfitBrowser.h"
#include "Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>
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

	// The Sort dropdown's rows, the current key highlighted with no tick mark;
	// choosing one sorts by it (OutfitBrowser::SetSort) and closes the popup. A
	// zero-high spacer as wide as the widest row keeps the popup, which starts as
	// narrow as its frame, from cutting a row's label.
	void DrawSortRows(const State& a_state, OutfitBrowser::State& a_browser)
	{
		const auto& style = ImGui::GetStyle();
		float       widest = 0.0f;
		for (const auto& option : kSortOptions) {
			widest = (std::max)(widest, Theme::DropdownRowWidth(option.label, false));
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));  // rows carry their own gap
		ImGui::Dummy(ImVec2(widest, 0.0f));
		for (const auto& option : kSortOptions) {
			bool ticked = a_browser.sort == option.key;
			if (DrawCategoryRow(option.label, ticked, false)) {
				if (a_browser.sort != option.key) {
					OutfitBrowser::SetSort(a_browser, a_state.request, option.key);
				}
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::PopStyleVar();
	}

	// "Sort by <key>" and the skin's arrow, bare, in the small font and greyed
	// as the fields' hints are, at the right end of the label just drawn with
	// its text's bottom on the label's. Its width fits the longest key, so
	// switching keys does not move it. It leaves the cursor where the label
	// did, so the list below does not move.
	void DrawSortCombo(const State& a_state, OutfitBrowser::State& a_browser)
	{
		const ImVec2 labelMax = ImGui::GetItemRectMax();
		const ImVec2 after = ImGui::GetCursorScreenPos();
		const float  right = after.x + ImGui::GetContentRegionAvail().x;
		const auto   preview = [](const SortOption& a_option) { return std::format("Sort by {}", a_option.label); };

		Theme::PushSmallFont();
		constexpr Theme::FieldStyle style{ .shape = Theme::FieldShape::kBare, .previewIsHint = true, .alignRight = true };
		float                       width = 0.0f;
		std::string                 current;
		for (const auto& option : kSortOptions) {
			width = (std::max)(width, Theme::ComboWidth(preview(option).c_str(), style));
			if (option.key == a_browser.sort) {
				current = preview(option);
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(right - width, labelMax.y - Theme::FieldHeight(style)));
		ImGui::SetNextItemWidth(width);
		const bool open = Theme::BeginCombo("##sort", ImGuiComboFlags_None, style);
		if (open) {
			DrawSortRows(a_state, a_browser);
		}
		Theme::EndCombo(open, current.c_str(), style);
		Theme::PopSmallFont();
		ImGui::SetCursorScreenPos(after);
	}

	// Height of the outfit list with every library outfit and every heading of the sort in
	// view, capped as ListHeight caps it, so narrowing the list does not move the buttons.
	// Contract: called with the list's row spacing pushed, outside BeginList at its indent
	// (`a_tagsWidth` is Theme::OutfitTagsWidth there).
	float OutfitListHeight(const State& a_state, const OutfitBrowser::State& a_browser, float a_tagsWidth, float a_reserved)
	{
		const auto&                    outfits = a_state.request.existingOutfits;
		const std::vector<std::string> none;
		const float                    minHeight = Theme::OutfitRowHeight(-1, none, a_tagsWidth);
		float                          rows = static_cast<float>(a_browser.headingCount) * Theme::FoldHeadingHeight();
		for (std::size_t i = 0; i < outfits.size(); ++i) {
			rows += a_state.showCategories ? Theme::OutfitRowHeight(outfits[i].gender, a_browser.tags[i], a_tagsWidth) : minHeight;
		}
		return FittedListHeight((std::max)(rows, minHeight), minHeight, a_reserved);
	}

	// One outfit of the list: its name, with its tags under it while Show Categories is on, and
	// at its right end the marks of the outfit the actor wears now or has as their Default and
	// of one chosen for a situation (Theme::OutfitMarks); `a_striped` for every second row
	// under a heading.
	bool DrawOutfitRow(const State& a_state, const OutfitBrowser::State& a_browser, int a_index, float a_tagsWidth, bool a_selected, bool a_striped, bool a_grip)
	{
		const auto&                    outfit = OutfitAt(a_state, a_index);
		const auto&                    situations = a_browser.situations[static_cast<std::size_t>(a_index)];
		const Theme::OutfitMarks       marks{ .current = outfit.id != 0 && outfit.id == a_state.request.wornOutfit,
			.defaultOutfit = HoldsOutfit(a_state, 0, outfit.id),
			.situations = situations.empty() ? nullptr : situations.c_str() };
		const std::vector<std::string> none;
		const auto&                    tags = a_state.showCategories ? a_browser.tags[static_cast<std::size_t>(a_index)] : none;
		const int                      gender = a_state.showCategories ? outfit.gender : -1;  // the gender is a tag too
		return Theme::OutfitRow(outfit.name.c_str(), gender, tags, a_tagsWidth, a_selected, marks, a_striped, a_grip);
	}

	// Left column of the outfit pages: the wardrobe's outfits that pass the
	// browser, in its order, under the sort's headings, `a_selected`
	// highlighted, under a label with the Sort dropdown, and Show Categories
	// under the list. A click on a heading folds its outfits away or back. The
	// list stays as tall as the whole wardrobe, so narrowing or folding it does
	// not move the buttons; `a_reserved` is the browser's height (ListHeight).
	// Returns the index into request.existingOutfits of the one clicked, or -1;
	// a click plays the focus sound. `a_draggable` (Manage Situations): each row has a grip
	// and drags a copy of its outfit, and a click selects nothing.
	// Names are shown as they are (Theme::SelectRow).
	int DrawOutfitList(State& a_state, OutfitBrowser::State& a_browser, int a_selected, float a_reserved, bool a_draggable)
	{
		const auto& outfits = a_state.request.existingOutfits;
		const auto& visible = a_browser.visible;
		if (visible.size() == outfits.size()) {
			Theme::LabelText(std::format("{} outfit(s) in the wardrobe", outfits.size()).c_str());
		} else {
			Theme::LabelText(std::format("{} of {} outfits", visible.size(), outfits.size()).c_str());
		}
		DrawSortCombo(a_state, a_browser);
		// No gap between rows, so every other row's stripe meets the next; the rows hold the gap
		// themselves (Theme::OutfitRow). Popped before EndList, as the piece list's spacing is.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
		const float tagsWidth = Theme::OutfitTagsWidth(a_draggable);
		// Show Categories, under the list, takes a frame's height too.
		const float listHeight = OutfitListHeight(a_state, a_browser, tagsWidth, a_reserved + ImGui::GetFrameHeightWithSpacing());
		int         clicked = -1;
		if (Theme::BeginList("outfits", listHeight)) {
			if (visible.empty()) {
				ImGui::TextDisabled("No outfit matches.");
			}
			const std::string* heading = nullptr;  // the last one drawn
			bool               folded = false;     // its outfits are folded away
			std::size_t        drawn = 0;          // rows drawn under it, for the stripes: every second one has one
			for (const int index : visible) {
				const auto& own = a_browser.headings[static_cast<std::size_t>(index)];
				if (!own.empty() && (!heading || *heading != own)) {
					folded = OutfitBrowser::IsFolded(a_browser, own);
					drawn = 0;
					if (Theme::FoldHeading(own.c_str(), !folded)) {
						QueueMenuSound(kFocusSound);
						OutfitBrowser::ToggleFolded(a_browser, own);
						folded = !folded;
					}
				}
				heading = &own;
				if (folded) {
					continue;
				}
				ImGui::PushID(index);
				if (DrawOutfitRow(a_state, a_browser, index, tagsWidth, a_selected == index, drawn++ % 2 == 1, a_draggable) && !a_draggable) {
					clicked = index;
				}
				if (a_draggable) {
					const auto& outfit = OutfitAt(a_state, index);
					OfferOutfitDrag({ .outfit = outfit.id }, outfit.name.c_str());
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
		if (clicked >= 0) {
			QueueMenuSound(kFocusSound);
		}
		if (Theme::Checkbox("Show Categories", &a_state.showCategories)) {
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

	// An outfit's piece count line. The count includes the pieces whose plugin is not
	// loaded, which stay in the outfit but have no row.
	std::string PieceCountText(std::size_t a_loaded, std::size_t a_unloaded)
	{
		const auto total = a_loaded + a_unloaded;
		return a_unloaded ? std::format("{} piece(s), {} not loaded", total, a_unloaded) : std::format("{} piece(s)", total);
	}

	// Pieces' names in read-only rows laid out as the outfit list's beside them.
	void DrawPieceNames(const State& a_state, const std::vector<std::size_t>& a_pieces)
	{
		PushListRowSpacing();  // as the outfit list beside it
		const float listHeight = ListHeight(a_pieces.size(), Theme::SelectRowHeight() + ImGui::GetStyle().ItemSpacing.y);
		if (Theme::BeginList("outfitPieces", listHeight)) {
			for (const auto index : a_pieces) {
				Theme::TextRow(a_state.request.pieces[index].name.c_str());
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
	}

	// The outfit's pieces as the library had them when the window opened.
	void DrawOutfitPieces(const State& a_state, const CaptureWindow::ExistingOutfit& a_outfit)
	{
		Theme::LabelText(PieceCountText(a_outfit.pieces.size(), a_outfit.unloadedPieces).c_str());
		DrawPieceNames(a_state, a_outfit.pieces);
	}

	// Right column of the Load and Select pages: the selected outfit's name, categories and
	// pieces, read only; `a_hint` while none is selected.
	void DrawOutfitDetails(const State& a_state, int a_selected, const char* a_hint)
	{
		if (!DrawSelectedName(a_state, a_selected, a_hint)) {
			return;
		}
		Theme::LabelText(CaptureWindow::kClassGroup);
		const auto&              outfit = OutfitAt(a_state, a_selected);
		std::vector<std::string> classes;
		for (const auto& option : a_state.request.categories) {
			if (IsClass(option) && std::ranges::find(outfit.categories, option.name) != outfit.categories.end()) {
				classes.push_back(option.name);
			}
		}
		ImGui::TextWrapped("%s", classes.empty() ? "None" : JoinNames(classes).c_str());
		Theme::AfterFieldGap();
		DrawOutfitPieces(a_state, outfit);
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
			controls = DrawBrowserControls(a_state, a_state.loadBrowser);
			clicked = DrawOutfitList(a_state, a_state.loadBrowser, a_state.previewedOutfit, controls.height);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			DrawOutfitDetails(a_state, a_state.previewedOutfit, "Select an outfit to load");
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

	// --- Select page ----------------------------------------------------------
	//
	// The Update tab's outfit selection, opened by "Update Existing Outfit...": the library
	// outfits, the selected one on the preview. Select fills the Update tab from it; Cancel
	// goes back to the tab the Save page showed, nothing changed.

	enum class SelectAction
	{
		kNone,
		kSelect,
		kCancel
	};

	// Select / Cancel centred below a gap. a_escapeTaken: the browser used this frame's Escape.
	SelectAction DrawSelectButtons(const State& a_state, bool a_escapeTaken)
	{
		BeginButtonRow({ "Select", "Cancel" });
		const bool select = ReasonButton("Select", a_state.selectingOutfit >= 0 ? nullptr : kNeedOutfit);
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken);

		if (select) {
			return SelectAction::kSelect;
		}
		return cancel ? SelectAction::kCancel : SelectAction::kNone;
	}

	// Selecting an outfit shows it on the preview; a second click on it deselects it. Leaving
	// shows the ticks again, which are what the Save page saves.
	void DrawSelectPage(State& a_state)
	{
		BrowserControls controls;
		int             clicked = -1;
		if (BeginColumns("outfits")) {
			ImGui::TableNextColumn();
			controls = DrawBrowserControls(a_state, a_state.selectBrowser);
			clicked = DrawOutfitList(a_state, a_state.selectBrowser, a_state.selectingOutfit, controls.height);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			DrawOutfitDetails(a_state, a_state.selectingOutfit, "Select an outfit to update");
			EndColumns();
		}
		if (clicked >= 0) {
			a_state.selectingOutfit = clicked == a_state.selectingOutfit ? -1 : clicked;
			PreviewOutfitOrTicks(a_state, a_state.selectingOutfit);
		}

		const auto action = DrawSelectButtons(a_state, controls.escapeTaken);
		EndButtonRow();
		if (action == SelectAction::kNone) {
			return;
		}
		QueueMenuSound(action == SelectAction::kSelect ? kOkSound : kCancelSound);
		if (a_state.selectingOutfit >= 0) {
			QueuePreview(a_state, TickedPieces(a_state));
		}
		if (action == SelectAction::kSelect) {
			ChooseOutfit(a_state, a_state.selectingOutfit);
		}
		GoToPage(a_state, Page::kSave);
	}

	// --- Manage Wardrobe ------------------------------------------------------
	//
	// The tab beside the Inventory page. The edits belong to the selected outfit and stay
	// until another is selected or the window closes; the preview shows them, or the
	// Inventory page's ticks while none is selected. The actor's rows for the outfit are
	// edited with it and take effect with Update or Apply.

	bool IsInClass(const CaptureWindow::ExistingOutfit& a_outfit, const CaptureWindow::CategoryOption& a_option)
	{
		return IsClass(a_option) && std::ranges::find(a_outfit.categories, a_option.name) != a_outfit.categories.end();
	}

	// The selected outfit's library id. Contract: an outfit is selected.
	std::uint32_t ManagedOutfitId(const State& a_state)
	{
		return OutfitAt(a_state, a_state.manage.outfit).id;
	}

	void FillManageForm(State& a_state, int a_index)
	{
		auto& form = a_state.manage;
		form.outfit = a_index;
		a_state.error.clear();
		if (a_index < 0) {
			form.pieces.clear();
			return;
		}
		const auto& outfit = OutfitAt(a_state, a_index);
		const auto& options = a_state.request.categories;
		form.name.fill('\0');
		std::strncpy(form.name.data(), outfit.name.c_str(), kNameCapacity - 1);
		for (std::size_t i = 0; i < options.size(); ++i) {
			form.classTicked[i] = IsInClass(outfit, options[i]);
		}
		form.gender = outfit.gender;
		form.pieces = outfit.pieces;
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			form.wearsFor[row] = HoldsOutfit(a_state, row, outfit.id);
		}
	}

	// Starts the edits of outfit `a_index` (-1 for none) and shows it on the preview, or
	// the Inventory page's ticks with none selected.
	void SelectManaged(State& a_state, int a_index)
	{
		FillManageForm(a_state, a_index);
		QueuePreview(a_state, a_index >= 0 ? a_state.manage.pieces : TickedPieces(a_state));
	}

	// The selected outfit's rows differ from the actor's: a row ticked that they do not have
	// it for, or one they have it for unticked.
	bool HasRowEdits(const State& a_state)
	{
		const auto& form = a_state.manage;
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			if (form.wearsFor[row] != HoldsOutfit(a_state, row, ManagedOutfitId(a_state))) {
				return true;
			}
		}
		return false;
	}

	// The selected outfit's edits differ from the library: its trimmed name, categories,
	// gender or loaded pieces.
	bool HasLibraryEdits(const State& a_state)
	{
		const auto& form = a_state.manage;
		const auto& outfit = OutfitAt(a_state, form.outfit);
		if (Trimmed(form.name.data()) != outfit.name || form.gender != outfit.gender || form.pieces != outfit.pieces) {
			return true;
		}
		const auto& options = a_state.request.categories;
		for (std::size_t i = 0; i < options.size(); ++i) {
			if (IsClass(options[i]) && form.classTicked[i] != IsInClass(outfit, options[i])) {
				return true;
			}
		}
		return false;
	}

	// Anything Update would save: library edits or row edits.
	bool HasManageEdits(const State& a_state)
	{
		return a_state.manage.outfit >= 0 && (HasLibraryEdits(a_state) || HasRowEdits(a_state));
	}

	// The line every unsaved-edits popup shows; "" with no outfit selected.
	std::string UnsavedText(const State& a_state)
	{
		return a_state.manage.outfit >= 0 ? std::format("You have unsaved changes for \"{}\".", OutfitAt(a_state, a_state.manage.outfit).name) : std::string{};
	}

	void DrawCategoryDropdown(const State& a_state, bool* a_ticked)
	{
		const auto& options = a_state.request.categories;
		if (std::ranges::none_of(options, IsClass)) {
			return;
		}
		Theme::LabelText(CaptureWindow::kClassGroup);
		const auto              ticked = JoinNames(TickedCategories(a_state, a_ticked));
		const Theme::FieldStyle style{ .previewIsHint = ticked.empty() };
		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool open = Theme::BeginCombo("##category", ImGuiComboFlags_HeightLargest, style);
		if (open) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));  // rows carry their own gap
			for (std::size_t i = 0; i < options.size(); ++i) {
				if (IsClass(options[i])) {
					DrawCategoryRow(options[i].name.c_str(), a_ticked[i]);
				}
			}
			ImGui::PopStyleVar();
		}
		Theme::EndCombo(open, ticked.empty() ? "None" : ticked.c_str(), style);
	}

	bool DrawRowsDropdown(const State& a_state, const char* a_label, bool* a_ticked)
	{
		Theme::LabelText(a_label);
		const bool  helpHovered = Theme::LabelHelpMark();
		std::string ticked;
		for (std::size_t row = 0; row < RowCount(a_state); ++row) {
			if (a_ticked[row]) {
				ticked += std::format("{}{}", ticked.empty() ? "" : ", ", RowName(a_state, row));
			}
		}
		const Theme::FieldStyle style{ .previewIsHint = ticked.empty() };
		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool open = Theme::BeginCombo("##situations", ImGuiComboFlags_HeightLargest, style);
		if (open) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));  // rows carry their own gap
			for (std::size_t row = 0; row < RowCount(a_state); ++row) {
				DrawCategoryRow(RowName(a_state, row), a_ticked[row]);
			}
			ImGui::PopStyleVar();
		}
		Theme::EndCombo(open, ticked.empty() ? "None" : ticked.c_str(), style);
		return helpHovered;
	}

	// "Situations for #{Name}": the rows the actor wears the selected outfit for, as edited.
	// The ticks are part of the edits.
	void DrawSituationsField(State& a_state)
	{
		if (DrawRowsDropdown(a_state, a_state.manageSituationsLabel.c_str(), a_state.manage.wearsFor.get())) {
			const auto& name = a_state.request.actorName;
			DrawHelpTooltip(std::format("Where {0} wears this outfit, saved with Update or Apply. These choices are {0}'s own: other characters keep theirs.", name));
		}
	}

	// Right column: the selected outfit's name, gender and categories as edited, the rows
	// the actor wears it for, then its pieces as edited under a count line ending in Edit.
	// Returns true when Edit is clicked.
	bool DrawManageForm(State& a_state)
	{
		auto& form = a_state.manage;
		if (form.outfit < 0) {
			Theme::LabelText("Select an outfit to edit");
			return false;
		}
		DrawNameField(a_state, form.name);
		DrawGenderField(form.gender);
		DrawCategoryDropdown(a_state, form.classTicked.get());
		DrawSituationsField(a_state);
		Theme::AfterFieldGap();
		Theme::LabelText(PieceCountText(form.pieces.size(), OutfitAt(a_state, form.outfit).unloadedPieces).c_str());
		const bool edit = Theme::LabelLink("Edit");
		DrawPieceNames(a_state, form.pieces);
		return edit;
	}

	enum class ManageAction
	{
		kNone,
		kUpdate,
		kRemove,
		kApply,
		kCancel
	};

	// The rows Apply gives the outfit: the ticked ones; the outfit another one wears is
	// replaced, as Apply's popup says. Contract: an outfit is selected.
	std::vector<std::size_t> ApplyRows(const State& a_state)
	{
		return AssignedRows(a_state, a_state.manage.wearsFor.get());
	}

	// `for Town, Warm` from the rows Apply gives the outfit, or `as #{Name}'s Default` with
	// none ticked, when it becomes the Default.
	std::string ApplyTarget(const State& a_state)
	{
		const auto rows = ApplyRows(a_state);
		if (rows.empty()) {
			return std::format("as {}'s Default", a_state.request.actorName);
		}
		std::string names;
		for (const auto row : rows) {
			names += std::format("{}{}", names.empty() ? "" : ", ", RowName(a_state, row));
		}
		return "for " + names;
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
		return HasLibraryEdits(a_state) && Trimmed(form.name.data()).empty() ? kNeedName : nullptr;
	}

	// Why Update is greyed, or nullptr: it needs unsaved edits and a name.
	const char* UpdateReason(const State& a_state)
	{
		if (!HasManageEdits(a_state)) {
			return a_state.manage.outfit < 0 ? kNeedOutfit : kNoChanges;
		}
		return Trimmed(a_state.manage.name.data()).empty() ? kNeedName : nullptr;
	}

	// Update / Remove / Apply to #{Name} / Exit centred below a gap. Update needs
	// Request::onManageUpdate (UpdateReason); Remove needs Request::onManageRemove
	// and a selected outfit; Apply needs Request::onManageApply (ApplyReason), and says what
	// it dresses the actor for on hover. a_escapeTaken: the browser used this frame's
	// Escape, which is otherwise Exit.
	ManageAction DrawManageButtons(const State& a_state, bool a_escapeTaken)
	{
		const char* noUpdate = UpdateReason(a_state);
		const char* apply = a_state.manageApplyLabel.c_str();
		const auto  applyTooltip = a_state.manage.outfit >= 0 ? std::format("Dress {} in this outfit {}.", a_state.request.actorName, ApplyTarget(a_state)) : std::string{};
		BeginButtonRow({ "Update", "Remove", apply, "Exit" });
		ImGui::BeginDisabled(!a_state.request.onManageUpdate);
		const bool update = ReasonButton("Update", noUpdate);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!a_state.request.onManageRemove);
		const bool remove = ReasonButton("Remove", a_state.manage.outfit < 0 ? kNeedOutfit : nullptr);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!a_state.request.onManageApply);
		const bool applied = ReasonButton(apply, ApplyReason(a_state), applyTooltip.c_str());
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		const bool cancel = Theme::Button("Exit") || escape;

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

	// A popup with UnsavedText and buttons `a_first` and `a_second`; Escape is the second.
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
	// the pieces, the categories, a rename, the gender, the unloaded pieces kept.
	void DrawLibraryChangeLines(const State& a_state)
	{
		const auto& form = a_state.manage;
		const auto& outfit = OutfitAt(a_state, form.outfit);
		if (form.pieces != outfit.pieces) {
			ImGui::TextUnformatted(std::format("Its {} piece(s) become the {} edited piece(s).", outfit.pieces.size(), form.pieces.size()).c_str());
		}
		const auto& options = a_state.request.categories;
		bool        classesChanged = false;
		for (std::size_t i = 0; i < options.size(); ++i) {
			classesChanged = classesChanged || (IsClass(options[i]) && form.classTicked[i] != IsInClass(outfit, options[i]));
		}
		if (classesChanged) {
			if (const auto classes = TickedCategories(a_state, form.classTicked.get()); classes.empty()) {
				ImGui::TextUnformatted("It will have no Category.");
			} else {
				ImGui::TextUnformatted(std::format("Its Category becomes: {}.", JoinNames(classes)).c_str());
			}
		}
		if (const auto name = Trimmed(form.name.data()); name != outfit.name) {
			ImGui::TextUnformatted(std::format("It is renamed \"{}\".", name).c_str());
		}
		if (form.gender != outfit.gender) {
			ImGui::TextUnformatted(std::format("Its gender becomes {}.", GenderName(form.gender)).c_str());
		}
		if (outfit.unloadedPieces) {
			ImGui::TextDisabled("%s", std::format("{} piece(s) from unloaded plugins stay in it.", outfit.unloadedPieces).c_str());
		}
	}

	// Update's popup: `Save your changes to "X"?`, the library and row changes (a ticked row
	// whose worn outfit it replaces reads `Town: "B" replaces "A"`), then Confirm / Cancel.
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
		DrawRowChangeLines(a_state, form.wearsFor.get(), ManagedOutfitId(a_state), Trimmed(form.name.data()));
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

	// Whether Apply needs the popup: with library edits (saved first), row changes (the
	// outfit a row wears may be replaced), or a Default whose worn outfit it would replace
	// with no row ticked.
	// Contract: an outfit is selected.
	bool ApplyNeedsConfirm(const State& a_state)
	{
		const bool replacesDefault = ApplyRows(a_state).empty() && IsTaken(a_state, 0, ManagedOutfitId(a_state));
		return HasLibraryEdits(a_state) || HasRowEdits(a_state) || replacesDefault;
	}

	// Apply's popup: `Apply "X" to #{Name} for Town, Warm?`, or with library edits `Save your
	// changes to "X", then apply it ...?` and their lines; the row changes; and, with no row
	// ticked, the Default it replaces. Then Update and Apply or Apply, and Cancel. Returns
	// true on the first.
	bool DrawManageApplyPopup(const State& a_state)
	{
		if (!BeginModal(kManageApplyPopup)) {
			return false;
		}
		const auto& form = a_state.manage;
		if (form.outfit < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		const auto  id = ManagedOutfitId(a_state);
		const auto& outfit = OutfitAt(a_state, form.outfit);
		const auto& name = a_state.request.actorName;
		const bool  edits = HasLibraryEdits(a_state);
		const auto  target = ApplyTarget(a_state);
		if (edits) {
			ImGui::TextUnformatted(std::format("Save your changes to \"{}\", then apply it to {} {}?", outfit.name, name, target).c_str());
			DrawLibraryChangeLines(a_state);
		} else {
			ImGui::TextUnformatted(std::format("Apply \"{}\" to {} {}?", outfit.name, name, target).c_str());
		}
		DrawRowChangeLines(a_state, form.wearsFor.get(), id, Trimmed(form.name.data()));
		if (ApplyRows(a_state).empty() && IsTaken(a_state, 0, id)) {
			ImGui::TextUnformatted(std::format("{} already has a Default outfit, {}. It will be replaced.", name, HeldOutfitName(a_state, 0)).c_str());
		}
		const char* applyLabel = edits ? "Update and Apply" : "Apply";
		BeginButtonRow({ applyLabel, "Cancel" });
		const bool apply = Theme::Button(applyLabel);
		ImGui::SameLine();
		const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		EndButtonRow();
		if (apply || cancel) {
			QueueMenuSound(apply ? kOkSound : kCancelSound);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return apply;
	}

	// Hands the edits and the rows to onManageUpdate, which writes the library, changes the
	// rows and ends with SetOutfits, or with `a_apply` dresses the actor and closes the
	// window, or ends with SetError; until then the page is greyed.
	// Contract: the handler writes nothing for a refused name or a failed write, and then
	// assigns nothing (CLAUDE.md §2.1).
	void RequestManageUpdate(State& a_state, bool a_apply)
	{
		const auto& form = a_state.manage;
		const auto  id = ManagedOutfitId(a_state);
		a_state.status = Status::kSaving;
		a_state.error.clear();
		CaptureWindow::SaveRequest edit{ .outfit = id,
			.name = Trimmed(form.name.data()),
			.gender = form.gender,
			.pieces = form.pieces,
			.categories = TickedCategories(a_state, form.classTicked.get()),
			.assign = AssignedRows(a_state, form.wearsFor.get()),
			.remove = RemovedRows(a_state, form.wearsFor.get(), id) };
		auto                       onUpdate = a_state.request.onManageUpdate;
		SKSE::GetTaskInterface()->AddTask([onUpdate, edit = std::move(edit), a_apply] { onUpdate(edit, a_apply); });
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
			ImGui::TextDisabled("Their situations without it fall back to the default; a default from it keeps its pieces.");
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

	// Apply with no library edits: hands the outfit and its rows to onManageApply, which
	// dresses the actor and closes the window, or ends with SetError; until then the page
	// is greyed. With library edits RequestManageUpdate saves them first.
	void RequestManageApply(State& a_state)
	{
		if (HasLibraryEdits(a_state)) {
			RequestManageUpdate(a_state, true);
			return;
		}
		const auto& form = a_state.manage;
		const auto  id = ManagedOutfitId(a_state);
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onApply = a_state.request.onManageApply;
		SKSE::GetTaskInterface()->AddTask(
			[onApply, id, assign = ApplyRows(a_state), remove = RemovedRows(a_state, form.wearsFor.get(), id)] { onApply(id, assign, remove); });
	}

	// The page's popups, each opened by the page in its own ID scope. The unsaved-changes
	// alert's Save Changes saves through Update's popup, whose Confirm the leave follows
	// once the save took (Draw); Discard Changes starts the edits again from the library.
	void DrawManagePopups(State& a_state)
	{
		if (DrawManageConfirmPopup(a_state)) {
			RequestManageUpdate(a_state, false);
			a_state.leaveAfterSave = a_state.leave.has_value();
		}
		if (DrawManageApplyPopup(a_state)) {
			RequestManageApply(a_state);
		}
		if (DrawRemovePopup(a_state)) {
			RequestManageRemove(a_state);
		}
		if (DrawUnsavedPopup(a_state, kDiscardPopup, "Discard changes", "Keep editing") == UnsavedChoice::kFirst) {
			SelectManaged(a_state, a_state.discardFor);
		}
		switch (DrawLeavePopup(a_state, UnsavedText(a_state), UpdateReason(a_state))) {
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
	// it deselects it. Either asks first while the selected one has unsaved edits. Apply asks
	// when it has anything to say (ApplyNeedsConfirm), else dresses at once.
	void DrawManagePage(State& a_state)
	{
		if (std::exchange(a_state.openLeave, false)) {
			ImGui::OpenPopup(kLeavePopup);
		}
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		BrowserControls controls;
		int             clicked = -1;
		bool            edit = false;
		if (BeginColumns("manage")) {
			ImGui::TableNextColumn();
			controls = DrawBrowserControls(a_state, a_state.manageBrowser);
			clicked = DrawOutfitList(a_state, a_state.manageBrowser, a_state.manage.outfit, controls.height);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			edit = DrawManageForm(a_state);
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
		if (edit) {
			QueueMenuSound(kOkSound);
			OpenEditPage(a_state);
		}
		if (action == ManageAction::kUpdate) {
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kManageConfirmPopup);
		} else if (action == ManageAction::kRemove) {
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kRemovePopup);
		} else if (action == ManageAction::kApply) {
			QueueMenuSound(kOkSound);
			if (ApplyNeedsConfirm(a_state)) {
				ImGui::OpenPopup(kManageApplyPopup);
			} else {
				RequestManageApply(a_state);
			}
		} else if (action == ManageAction::kCancel) {
			RequestLeave(a_state, { .exit = true });
		}
		DrawManagePopups(a_state);
	}

	// --- Edit page ------------------------------------------------------------

	PieceList EditPieces(const State& a_state)
	{
		return { a_state.editTicked.get(), &a_state.editShown, &a_state.editShownHeading };
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
		a_state.editShownHeading = std::format("From \"{}\" (Not Carried)", name);
		GoToPage(a_state, Page::kEdit);
	}

	// Use Ticked Pieces makes the ticks the edited pieces, which the preview already shows;
	// Back and Escape leave them, and the preview shows them again. Nothing is written.
	void DrawEditPage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		const auto list = EditPieces(a_state);
		DrawPieceRows(a_state, list);
		DrawPieceCount(a_state, list);
		DrawPreviewToggle(a_state, list);
		BeginButtonRow({ "Use Ticked Pieces", "Back" });
		const auto ticked = Ticked(a_state, list);
		const bool use = ReasonButton("Use Ticked Pieces", ticked.empty() ? kNeedPiece : nullptr);
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
		EndButtonRow();
		ImGui::EndDisabled();

		if (use) {
			QueueMenuSound(kOkSound);
			a_state.manage.pieces = ticked;
			GoToPage(a_state, Page::kManage);
		} else if (back) {
			QueueMenuSound(kCancelSound);
			QueuePreview(a_state, a_state.manage.pieces);
			GoToPage(a_state, Page::kManage);
		}
	}
}
