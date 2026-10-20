// SPDX-License-Identifier: GPL-3.0-or-later
//
// The capture window's outfit pages (see CaptureWindow.cpp for the window and the
// Inventory and Save pages, CaptureWindowInternal.h for the state they share).
//
// FILE MAP
//   Outfit browser - Search (and Filter, hidden by kShowFilter), above both outfit pages'
//                  list in its column, in the small font (OutfitBrowser.cpp narrows and
//                  orders it); Escape first clears the search
//   Outfit lists - the library outfit list with its Sort dropdown, the sort's headings and
//                  each outfit's tags (off with Show Categories under it), the selected
//                  outfit's name (or a hint while none is) and its piece list, which
//                  both outfit pages share;
//                  on both, the selected outfit is on the preview, and a second
//                  click on it, or Back, shows the ticks again
//   Load page    - library outfits and the selected one's details; Load ticks its
//                  pieces and returns (greyed while none of them is loaded)
//   Select page  - the Update tab's outfit selection: library outfits and the selected
//                  one's details; Select fills the Update tab (ChooseOutfit), Cancel
//                  leaves it as it was
#include "PCH.h"

#include "CaptureWindowInternal.h"

#include "OutfitBrowser.h"
#include "Theme.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
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

	// Search over Filter (while kShowFilter), in the small font, with no labels, at the top
	// of the outfit list's column; refreshes the list only when one of them changed.
	BrowserControls DrawBrowserControls(State& a_state)
	{
		constexpr float kGapBelowRow = 4.0f;  // pixels at 1080p, under the row on top of the item spacing

		const float top = ImGui::GetCursorPosY();
		Theme::SectionGap();  // the heading leaves its last gap to the page's first field (Theme::HeadingText)
		BrowserInput input;
		ImGui::PushID("browser");
		Theme::PushSmallFont();
		DrawSearchField(a_state, input);
		if (kShowFilter) {
			DrawFilterField(a_state, input);
		}
		Theme::PopSmallFont();
		ImGui::PopID();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kGapBelowRow * Theme::Scale());
		if (input.changed) {
			OutfitBrowser::Refresh(a_state.browser, a_state.request);
		}
		return { input.escapeTaken, ImGui::GetCursorPosY() - top };
	}

	// --- Outfit lists ---------------------------------------------------------

	bool OffersSort(const State& a_state, const SortOption& a_option)
	{
		return OutfitBrowser::OffersSort(a_state.request, a_option.key);
	}

	// The Sort dropdown's rows, the current key highlighted with no tick mark;
	// choosing one closes the popup. A zero-high spacer as wide as the widest
	// row keeps the popup, which starts as narrow as its frame, from cutting a
	// row's label. Returns true when the key changed.
	bool DrawSortRows(State& a_state)
	{
		const auto& style = ImGui::GetStyle();
		float       widest = 0.0f;
		for (const auto& option : kSortOptions) {
			if (OffersSort(a_state, option)) {
				widest = (std::max)(widest, ImGui::CalcTextSize(option.label).x);
			}
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));  // rows carry their own gap
		ImGui::Dummy(ImVec2(widest + style.FramePadding.x * 2.0f, 0.0f));
		bool changed = false;
		for (const auto& option : kSortOptions) {
			if (!OffersSort(a_state, option)) {
				continue;
			}
			bool ticked = a_state.browser.sort == option.key;
			if (DrawCategoryRow(option.label, ticked, false)) {
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
			if (!OffersSort(a_state, option)) {
				continue;
			}
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

	// Height of the outfit list with every library outfit and every heading of the sort in
	// view, capped as ListHeight caps it, so narrowing the list does not move the buttons.
	// Contract: called with the list's row spacing pushed, outside BeginList at its indent
	// (`a_tagsWidth` is Theme::OutfitTagsWidth there).
	float OutfitListHeight(const State& a_state, float a_tagsWidth, float a_reserved)
	{
		const auto& browser = a_state.browser;
		const auto& outfits = a_state.request.existingOutfits;
		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		const float minHeight = Theme::SelectRowHeight() + spacing;
		float       rows = static_cast<float>(browser.headingCount) * (Theme::ListHeadingHeight() + spacing);
		for (std::size_t i = 0; i < outfits.size(); ++i) {
			const float row = a_state.showCategories ? Theme::OutfitRowHeight(outfits[i].gender, browser.tags[i], a_tagsWidth) : Theme::SelectRowHeight();
			rows += row + spacing;
		}
		return FittedListHeight((std::max)(rows, minHeight), minHeight, a_reserved);
	}

	// One outfit of the list: its name, with its tags under it while Show Categories is on.
	bool DrawOutfitRow(const State& a_state, int a_index, float a_tagsWidth, bool a_selected)
	{
		const auto& outfit = OutfitAt(a_state, a_index);
		if (!a_state.showCategories) {
			return Theme::SelectRow(outfit.name.c_str(), a_selected);
		}
		const auto& tags = a_state.browser.tags[static_cast<std::size_t>(a_index)];
		return Theme::OutfitRow(outfit.name.c_str(), outfit.gender, tags, a_tagsWidth, a_selected);
	}

	// Left column of the Load and Update pages: the library's outfits that pass
	// the browser, in its order, under the sort's headings, `a_selected`
	// highlighted, under a label with the Sort dropdown, and Show Categories
	// under the list. The list stays as tall as the whole library, so narrowing
	// it does not move the buttons; `a_reserved` is the browser's height
	// (ListHeight). Returns the index into request.existingOutfits of the one
	// clicked, or -1; a click plays the focus sound.
	// Names are shown as they are (Theme::SelectRow).
	int DrawOutfitList(State& a_state, int a_selected, float a_reserved)
	{
		const auto& outfits = a_state.request.existingOutfits;
		const auto& browser = a_state.browser;
		const auto& visible = browser.visible;
		if (visible.size() == outfits.size()) {
			Theme::LabelText(std::format("{} outfit(s) in the library", outfits.size()).c_str());
		} else {
			Theme::LabelText(std::format("{} of {} outfits", visible.size(), outfits.size()).c_str());
		}
		if (DrawSortCombo(a_state)) {
			OutfitBrowser::Refresh(a_state.browser, a_state.request);
		}
		PushListRowSpacing();  // the piece list's row gap; popped before EndList, as there
		const float tagsWidth = Theme::OutfitTagsWidth();
		// Show Categories, under the list, takes a frame's height too.
		const float listHeight = OutfitListHeight(a_state, tagsWidth, a_reserved + ImGui::GetFrameHeightWithSpacing());
		int         clicked = -1;
		if (Theme::BeginList("outfits", listHeight)) {
			if (visible.empty()) {
				ImGui::TextDisabled("No outfit matches.");
			}
			const std::string* heading = nullptr;  // the last one drawn
			for (const int index : visible) {
				const auto& own = browser.headings[static_cast<std::size_t>(index)];
				if (!own.empty() && (!heading || *heading != own)) {
					Theme::ListHeading(own.c_str());
				}
				heading = &own;
				ImGui::PushID(index);
				if (DrawOutfitRow(a_state, index, tagsWidth, a_selected == index)) {
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

	// The outfit's pieces as the library had them when the window opened, in rows
	// laid out as the outfit list's beside it. The count includes the pieces
	// whose plugin is not loaded, which have no row.
	void DrawOutfitPieces(const State& a_state, const CaptureWindow::ExistingOutfit& a_outfit)
	{
		const auto total = a_outfit.pieces.size() + a_outfit.unloadedPieces;
		const auto count = a_outfit.unloadedPieces ? std::format("{} piece(s), {} not loaded", total, a_outfit.unloadedPieces) :
		                                             std::format("{} piece(s)", total);
		Theme::LabelText(count.c_str());
		PushListRowSpacing();  // as the outfit list beside it
		const float listHeight = ListHeight(a_outfit.pieces.size(), Theme::SelectRowHeight() + ImGui::GetStyle().ItemSpacing.y);
		if (Theme::BeginList("outfitPieces", listHeight)) {
			for (const auto index : a_outfit.pieces) {
				Theme::TextRow(a_state.request.pieces[index].name.c_str());
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
	}

	// Right column of the Load and Select pages: the selected outfit's name, Armor Classes
	// and pieces, read only; `a_hint` while none is selected.
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
			controls = DrawBrowserControls(a_state);
			clicked = DrawOutfitList(a_state, a_state.previewedOutfit, controls.height);
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
			controls = DrawBrowserControls(a_state);
			clicked = DrawOutfitList(a_state, a_state.selectingOutfit, controls.height);
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
}
