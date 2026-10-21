// SPDX-License-Identifier: GPL-3.0-or-later
//
// The capture window's Manage Situations page (docs/situation-management-ui.md §3-§6; see
// CaptureWindow.cpp for the window, CaptureWindowInternal.h for the state the pages share).
//
// FILE MAP
//   Constants       - the drag's open delay, the page's popups, texts and reasons
//   Edits           - the form from the request, what differs from it, a name's rule at the
//                     end of its edit
//   Drag and drop   - an outfit dragged from either pane, dropped on a situation (at its front,
//                     or between two of its outfits) or on the outfit list (taken off its row);
//                     a folded heading opens once a drag has hovered it a while
//   Situation List  - the left pane: a heading per situation (a click or Edit selects, its
//                     chevron folds) over the outfits it shows; a click on an outfit row of
//                     either pane shows its details and puts it on the preview
//   Right pane      - the outfit list with what a drag does under it; the selected situation's
//                     details: name, description, conditions, Random Outfit (for #{Name}), Show
//                     Outfit List with Remove; or an outfit's details with Edit and Back
//   Buttons, popups - Restore Presets (?), Save Changes, Exit; Remove's and Restore Presets'
//                     popups and the unsaved-changes alert, each handing its write to the
//                     Request's handler
#include "PCH.h"

#include "CaptureWindowInternal.h"

#include "OutfitBrowser.h"
#include "OutfitRows.h"
#include "Theme.h"
#include "Utils.h"

#include <imgui.h>
#include <imgui_internal.h>  // BeginDragDropTargetCustom, ImRect, GetActiveID

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CaptureWindow::Internal
{
	namespace
	{
		// --- Constants --------------------------------------------------------

		// How long a drag hovers a folded heading before it opens (seconds).
		constexpr double kDragExpandDelay = 1.0;
		// Room above a button row in the right pane (Show Outfit List; an outfit's Edit and Back),
		// beyond the item spacing (pixels at 1080p).
		constexpr float kPaneButtonGap = 12.0f;

		constexpr const char* kRemoveSituationPopup = "Remove situation?";
		constexpr const char* kRestorePresetsPopup = "Restore presets?";
		// Reasons a button is greyed out, shown on hover.
		constexpr const char* kNothingChanged = "Nothing has changed.";
		constexpr const char* kAsInstalled = "Every preset situation is as installed.";
		// The name a situation gets when neither the one typed nor the one before can be used.
		constexpr const char* kUnnamed = "Unnamed Situation";
		constexpr const char* kDiscardNote = "Your unsaved changes are discarded.";
		// Lines in an outfit row's place under a heading.
		constexpr const char* kPiecesLine = "Pieces not in the wardrobe";
		constexpr const char* kGoneLine = "Outfit no longer in the wardrobe";

		// --- Edits ------------------------------------------------------------

		void CopyToField(std::array<char, kNameCapacity>& a_field, const std::string& a_text)
		{
			a_field.fill('\0');
			std::strncpy(a_field.data(), a_text.c_str(), kNameCapacity - 1);
		}

		// A situation's name or description differs from situations.json.
		bool WordsChanged(const State& a_state)
		{
			const auto& entries = a_state.situationForm.entries;
			for (std::size_t i = 0; i < entries.size(); ++i) {
				const auto& saved = a_state.request.situations[i];
				if (Trimmed(entries[i].name.data()) != saved.name || Trimmed(entries[i].description.data()) != saved.description) {
					return true;
				}
			}
			return false;
		}

		// A row differs from the actor's, or the Default's pieces were dropped.
		bool RowsChanged(const State& a_state)
		{
			const auto& form = a_state.situationForm;
			for (std::size_t i = 0; i < form.entries.size(); ++i) {
				if (form.entries[i].row != a_state.request.assigned[i].row) {
					return true;
				}
			}
			return !form.entries.empty() && form.defaultPieces != a_state.request.assigned[0].pieces;
		}

		// The end of an edit of entry `a_index`'s name: an empty name, or one another situation
		// has (ASCII case ignored), gives way to the name it had as the edit began, or with that
		// unusable too, kUnnamed. The name is kept trimmed.
		void EndNameEdit(SituationForm& a_form, std::size_t a_index)
		{
			const auto taken = [&a_form, a_index](const std::string& a_name) {
				for (std::size_t j = 0; j < a_form.entries.size(); ++j) {
					if (j != a_index && ExampleMod::CaseInsensitiveEqual(Trimmed(a_form.entries[j].name.data()), a_name)) {
						return true;
					}
				}
				return false;
			};
			auto& entry = a_form.entries[a_index];
			auto  name = Trimmed(entry.name.data());
			if (name.empty() || taken(name)) {
				name = Trimmed(entry.nameBefore.c_str());
				if (name.empty() || taken(name)) {
					name = kUnnamed;
				}
			}
			CopyToField(entry.name, name);
		}

		// A name field that is no longer active ends its edit, before anything this frame reads
		// the name; it may have gone from view, its situation unselected.
		void EndInactiveNameEdit(SituationForm& a_form)
		{
			if (a_form.nameEditing >= 0 && ImGui::GetActiveID() != a_form.nameField) {
				if (static_cast<std::size_t>(a_form.nameEditing) < a_form.entries.size()) {
					EndNameEdit(a_form, static_cast<std::size_t>(a_form.nameEditing));
				}
				a_form.nameEditing = -1;
			}
		}

		// --- Drag and drop ----------------------------------------------------

		// The outfit the drag on now carries; nullptr for none.
		const OutfitPayload* DraggedOutfit()
		{
			const auto* payload = ImGui::GetDragDropPayload();
			return payload && payload->IsDataType(kOutfitPayload) ? static_cast<const OutfitPayload*>(payload->Data) : nullptr;
		}

		// Where a drop puts the outfit: a line across the Situation List at `a_y`.
		void DrawInsertLine(float a_y)
		{
			constexpr float kLineWidth = 2.0f;  // pixels at 1080p

			const float left = ImGui::GetWindowPos().x;
			ImGui::GetWindowDrawList()->AddLine(ImVec2(left, a_y), ImVec2(left + ImGui::GetWindowWidth(), a_y), ImGui::GetColorU32(ImGuiCol_CheckMark),
				(std::max)(1.0f, kLineWidth * Theme::Scale()));
		}

		// Accepts the drag over the drop target just begun when it carries an outfit (the
		// Default's pieces go only to the outfit list), with the line at `a_lineY` while it
		// hovers, and ends the target. Returns the payload once dropped.
		// Contract: right after a BeginDragDropTarget that returned true.
		std::optional<OutfitPayload> AcceptOnSituation(float a_lineY)
		{
			std::optional<OutfitPayload> dropped;
			const auto*                  dragged = DraggedOutfit();
			if (dragged && dragged->outfit != 0) {
				if (const auto* payload = ImGui::AcceptDragDropPayload(kOutfitPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
					DrawInsertLine(a_lineY);
					if (payload->IsDelivery()) {
						dropped = *static_cast<const OutfitPayload*>(payload->Data);
					}
				}
			}
			ImGui::EndDragDropTarget();
			return dropped;
		}

		// An outfit dropped on situation `a_target`, before what it shows at `a_index` (the
		// front with random off, where only `chosen` shows): from the outfit list a copy; from a
		// situation moved, off its row first (its `chosen` 0 if it was that), or within its own
		// row reordered. An outfit made the Default's `chosen` drops its pieces, as
		// Assignments does.
		void DropOnSituation(SituationForm& a_form, std::size_t a_target, std::size_t a_index, const OutfitPayload& a_payload)
		{
			if (a_payload.from >= 0 && static_cast<std::size_t>(a_payload.from) != a_target) {
				OutfitRows::Take(a_form.entries[static_cast<std::size_t>(a_payload.from)].row, a_payload.outfit);
			}
			auto& row = a_form.entries[a_target].row;
			OutfitRows::Place(row, a_payload.outfit, row.random ? a_index : 0);
			if (a_target == 0 && !row.random) {
				a_form.defaultPieces = false;
			}
		}

		// A drag from the Situation List dropped on the outfit list: the outfit leaves its row
		// (its `chosen` 0 if it was that), or the Default's pieces go.
		void DropOnOutfitList(SituationForm& a_form, const OutfitPayload& a_payload)
		{
			if (a_payload.from < 0) {
				return;
			}
			if (a_payload.outfit == 0) {
				a_form.defaultPieces = false;
				return;
			}
			OutfitRows::Take(a_form.entries[static_cast<std::size_t>(a_payload.from)].row, a_payload.outfit);
		}

		// --- Situation List ---------------------------------------------------

		// Back from an outfit's details: the right pane shows what it showed before them, and
		// the preview the Inventory page's ticks again.
		void CloseOutfitView(State& a_state)
		{
			a_state.situationForm.viewedOutfit = 0;
			QueuePreview(a_state, TickedPieces(a_state));
		}

		// A click on an outfit row of either pane: its details on the right and the outfit on the
		// preview; on the outfit whose details show, back, as Back does.
		void ViewOutfit(State& a_state, int a_index)
		{
			const auto& outfit = OutfitAt(a_state, a_index);
			if (a_state.situationForm.viewedOutfit == outfit.id) {
				CloseOutfitView(a_state);
				return;
			}
			a_state.situationForm.viewedOutfit = outfit.id;
			QueuePreview(a_state, outfit.pieces);
		}

		// What a heading shows under it: every outfit on the list with random on, `chosen`
		// alone with it off.
		std::vector<std::uint32_t> ShownOutfits(const OutfitRows::Row& a_row)
		{
			if (a_row.random) {
				return a_row.outfits;
			}
			return a_row.chosen != 0 ? std::vector<std::uint32_t>{ a_row.chosen } : std::vector<std::uint32_t>{};
		}

		// The Default wears its pieces not in the wardrobe: it has them and shows no outfit.
		bool ShowsPieces(const SituationForm& a_form, std::size_t a_entry)
		{
			return a_entry == 0 && a_form.defaultPieces && ShownOutfits(a_form.entries[0].row).empty();
		}

		// Something shows under situation `a_entry`'s heading: an outfit, or the Default's
		// pieces. One with nothing wears nothing: its heading has no chevron and a greyed name.
		bool ShowsAnything(const SituationForm& a_form, std::size_t a_entry)
		{
			return !ShownOutfits(a_form.entries[a_entry].row).empty() || ShowsPieces(a_form, a_entry);
		}

		// A grey line in an outfit row's place (kPiecesLine, kGoneLine).
		void DrawLineRow(const char* a_text, float a_tagsWidth, Theme::Grip a_grip)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			Theme::OutfitRow(a_text, -1, {}, a_tagsWidth, false, {}, false, a_grip);
			ImGui::PopStyleColor();
		}

		// An outfit row's height in the panes: with its tags while they show (Show Tags).
		float ShownRowHeight(const State& a_state, std::uint32_t a_outfit, float a_tagsWidth)
		{
			const int index = IndexOfOutfit(a_state, a_outfit);
			if (index < 0 || !a_state.showTags) {
				return Theme::OutfitRowHeight(-1, {}, a_tagsWidth);
			}
			return Theme::OutfitRowHeight(OutfitAt(a_state, index).gender, a_state.situationBrowser.tags[static_cast<std::size_t>(index)], a_tagsWidth);
		}

		// The Situation List's height with every heading open, capped as ListHeight caps it, and
		// at least the right pane's with the outfit list, so neither folding nor the right pane
		// moves the buttons; an outfit's details keep within it (DrawOutfitView).
		// Contract: with no item spacing pushed, outside BeginList at its indent, as
		// Theme::OutfitTagsWidth gave `a_tagsWidth`.
		float SituationListHeight(const State& a_state, float a_tagsWidth)
		{
			const auto& form = a_state.situationForm;
			float       rows = Theme::ListHeadingHeight();
			for (std::size_t i = 0; i < form.entries.size(); ++i) {
				rows += Theme::SituationHeadingHeight();
				if (ShowsPieces(form, i)) {
					rows += Theme::OutfitRowHeight(-1, {}, a_tagsWidth);
				}
				for (const auto outfit : ShownOutfits(form.entries[i].row)) {
					rows += ShownRowHeight(a_state, outfit, a_tagsWidth);
				}
			}
			return (std::max)(FittedListHeight(rows, Theme::ListHeadingHeight()), form.outfitPaneHeight);
		}

		// The outfits situation `a_entry` shows, each with a grip, dragged to move it, and a drop
		// place: before it on its upper half, after it on its lower half (random on; the front
		// with it off). With none, the Default's pieces line, which drags to the outfit list and
		// takes a drop at the front, or nothing.
		void DrawShownOutfits(State& a_state, std::size_t a_entry, float a_tagsWidth)
		{
			auto&      form = a_state.situationForm;
			const auto shown = ShownOutfits(form.entries[a_entry].row);
			if (shown.empty()) {
				if (!ShowsPieces(form, a_entry)) {
					return;
				}
				const float top = ImGui::GetCursorScreenPos().y;
				DrawLineRow(kPiecesLine, a_tagsWidth, Theme::Grip::kNone);
				OfferOutfitDrag({ .from = 0 }, kPiecesLine);
				if (ImGui::BeginDragDropTarget()) {
					if (const auto dropped = AcceptOnSituation(top)) {
						DropOnSituation(form, a_entry, 0, *dropped);
					}
				}
				return;
			}
			const bool random = form.entries[a_entry].row.random;
			for (std::size_t k = 0; k < shown.size(); ++k) {
				ImGui::PushID(static_cast<int>(k));
				const float top = ImGui::GetCursorScreenPos().y;
				const int   index = IndexOfOutfit(a_state, shown[k]);
				if (index >= 0) {
					// No star: every row here is one chosen for a situation.
					if (DrawOutfitRow(a_state, a_state.situationBrowser, index, a_tagsWidth, form.viewedOutfit == shown[k], k % 2 == 1, Theme::Grip::kName, false)) {
						QueueMenuSound(kFocusSound);
						ViewOutfit(a_state, index);
					}
				} else {
					DrawLineRow(kGoneLine, a_tagsWidth, Theme::Grip::kName);
				}
				const float bottom = ImGui::GetItemRectMax().y;
				OfferOutfitDrag({ .from = static_cast<int>(a_entry), .outfit = shown[k] }, index >= 0 ? OutfitAt(a_state, index).name.c_str() : kGoneLine);
				if (ImGui::BeginDragDropTarget()) {
					const bool after = random && ImGui::GetMousePos().y >= (top + bottom) * 0.5f;
					if (const auto dropped = AcceptOnSituation(after ? bottom : top)) {
						DropOnSituation(form, a_entry, after ? k + 1 : k, *dropped);
					}
				}
				ImGui::PopID();
			}
		}

		// Situation `a_entry`'s heading, then, unless folded, what it shows. A click left of the
		// chevron selects it and shows its details, closing an outfit's, or unselects it when its
		// details show; Edit always selects it and shows them; the chevron folds it. Its count shows
		// with random on alone; with nothing to show it has no chevron, is never folded (a drop
		// shows what it got) and its name is greyed. A drop on the heading, its rule included,
		// adds at the front. `a_dragHovered` becomes the entry while a drag hovers its folded
		// heading, which opens once DrawSituationList has seen that for kDragExpandDelay.
		void DrawSituationEntry(State& a_state, std::size_t a_entry, float a_tagsWidth, int& a_dragHovered)
		{
			auto&      form = a_state.situationForm;
			auto&      entry = form.entries[a_entry];
			const int  index = static_cast<int>(a_entry);
			const bool shows = ShowsAnything(form, a_entry);
			if (!shows) {
				entry.folded = false;
			}
			const float top = ImGui::GetCursorScreenPos().y;
			const auto  click = Theme::SituationHeading(entry.name.data(),
				{ .count = entry.row.random ? static_cast<int>(entry.row.outfits.size()) : -1,
					.open = !entry.folded,
					.foldable = shows,
					.selected = form.selected == index,
					.dim = !shows });
			const float bottom = top + Theme::SituationHeadingHeight();
			if (form.scrollToSelected && form.selected == index) {
				ImGui::SetScrollHereY(0.0f);
				form.scrollToSelected = false;
			}
			if (click == Theme::HeadingClick::kFold) {
				QueueMenuSound(kFocusSound);
				entry.folded = !entry.folded;
			} else if (click == Theme::HeadingClick::kSelect || click == Theme::HeadingClick::kEdit) {
				QueueMenuSound(kFocusSound);
				const bool viewing = form.viewedOutfit != 0;
				const bool unselect = click == Theme::HeadingClick::kSelect && form.selected == index && form.details && !viewing;
				if (viewing) {
					CloseOutfitView(a_state);
				}
				form.selected = unselect ? -1 : index;
				form.details = !unselect;
			}
			const float  left = ImGui::GetWindowPos().x;
			const ImRect heading(ImVec2(left, top), ImVec2(left + ImGui::GetWindowWidth(), bottom));
			if (ImGui::BeginDragDropTargetCustom(heading, ImGui::GetID("headingDrop"))) {
				if (entry.folded && DraggedOutfit()) {
					a_dragHovered = index;
					if (form.dragHover == index && ImGui::GetTime() - form.dragHoverSince >= kDragExpandDelay) {
						entry.folded = false;
					}
				}
				if (const auto dropped = AcceptOnSituation(bottom)) {
					DropOnSituation(form, a_entry, 0, *dropped);
				}
			}
			if (!entry.folded) {
				DrawShownOutfits(a_state, a_entry, a_tagsWidth);
			}
		}

		// The left pane: Situation List (?) over a heading per situation, the Default first.
		// Returns the list's bottom (screen Y), which an outfit's details keep above.
		float DrawSituationList(State& a_state)
		{
			auto& form = a_state.situationForm;
			// No gap between rows: the headings and rows hold it themselves (Theme::OutfitRow).
			// Popped before EndList, as the outfit list's spacing is.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
			const float tagsWidth = Theme::OutfitTagsWidth(Theme::Grip::kName);
			int         dragHovered = -1;
			if (Theme::BeginList("situations", SituationListHeight(a_state, tagsWidth), false)) {
				// Its text starts where the situations' names do, clear of a selected one's mark.
				if (Theme::ListHeadingHelp("Situation List", true)) {
					DrawHelpTooltip(std::format(
						"Each situation lists the outfits {} can wear there; the first one that applies is worn, else the "
						"Default. Drag an outfit from the outfit list onto a situation to add it, between two of its outfits "
						"to place it, onto another situation to move it, or back to the outfit list to take it off.",
						a_state.request.actorName));
				}
				for (std::size_t i = 0; i < form.entries.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					DrawSituationEntry(a_state, i, tagsWidth, dragHovered);
					ImGui::PopID();
				}
			}
			ImGui::PopStyleVar();
			Theme::EndList();
			const float bottom = ImGui::GetItemRectMax().y;
			if (form.dragHover != dragHovered) {
				form.dragHover = dragHovered;
				form.dragHoverSince = ImGui::GetTime();
			}
			return bottom;
		}

		// --- Right pane -------------------------------------------------------

		enum class RightPane
		{
			kOutfitList,
			kSituation,  // the selected situation's details
			kOutfit      // the details of the outfit clicked last
		};

		// What the right pane shows: the outfit list while a drag from the Situation List is on;
		// else the outfit clicked last, until Back; else the selected situation, until Show
		// Outfit List; else the outfit list.
		RightPane ShownPane(const State& a_state)
		{
			const auto& form = a_state.situationForm;
			const auto* dragged = DraggedOutfit();
			if (dragged && dragged->from >= 0) {
				return RightPane::kOutfitList;
			}
			if (IndexOfOutfit(a_state, form.viewedOutfit) >= 0) {
				return RightPane::kOutfit;
			}
			return form.selected >= 0 && form.details ? RightPane::kSituation : RightPane::kOutfitList;
		}

		// The outfit list with its own search, sort and Show Tags, its rows dragged as copies and
		// a click on one showing its details, then what a drag does, in the small font. A drag
		// from the Situation List dropped anywhere on it takes that outfit, or the Default's
		// pieces, off its row. Returns whether Escape cleared the search.
		bool DrawOutfitPane(State& a_state)
		{
			constexpr float       kFrameWidth = 2.0f;  // pixels at 1080p, around the pane while a drop would take an outfit off
			constexpr const char* kDragNote = "Drag an outfit onto a situation to add it.";

			const ImVec2 top = ImGui::GetCursorScreenPos();
			const float  width = ImGui::GetContentRegionAvail().x;
			const auto   controls = DrawBrowserControls(a_state, a_state.situationBrowser);
			Theme::PushSmallFont();
			const float noteHeight = ImGui::GetTextLineHeightWithSpacing();
			Theme::PopSmallFont();
			const int viewed = IndexOfOutfit(a_state, a_state.situationForm.viewedOutfit);
			const int clicked = DrawOutfitList(a_state, a_state.situationBrowser, viewed, controls.height + noteHeight, true);
			Theme::PushSmallFont();
			ImGui::TextWrapped("%s", kDragNote);
			Theme::PopSmallFont();
			const float bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
			a_state.situationForm.outfitPaneHeight = bottom - top.y;
			const ImRect pane(top, ImVec2(top.x + width, bottom));
			if (ImGui::BeginDragDropTargetCustom(pane, ImGui::GetID("outfitListDrop"))) {
				const auto* dragged = DraggedOutfit();
				if (dragged && dragged->from >= 0) {
					if (const auto* payload = ImGui::AcceptDragDropPayload(kOutfitPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
						ImGui::GetWindowDrawList()->AddRect(pane.Min, pane.Max, ImGui::GetColorU32(ImGuiCol_CheckMark), 0.0f, ImDrawFlags_None,
							(std::max)(1.0f, kFrameWidth * Theme::Scale()));
						if (payload->IsDelivery()) {
							DropOnOutfitList(a_state.situationForm, *static_cast<const OutfitPayload*>(payload->Data));
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (clicked >= 0) {
				ViewOutfit(a_state, clicked);  // DrawOutfitList played the focus sound
			}
			return controls.escapeTaken;
		}

		// Text in the small heading style: the small font, greyed, wrapped.
		void DrawNote(const std::string& a_text)
		{
			Theme::PushSmallFont();
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", a_text.c_str());
			ImGui::PopStyleColor();
			Theme::PopSmallFont();
		}

		// The settings every actor shares: the name (read only for the Default, which says so on
		// hover), the description and the sentence it ends, the conditions, read only.
		void DrawGlobalSettings(State& a_state, std::size_t a_entry)
		{
			constexpr const char* kDefaultName = "The Default's name cannot be changed.";

			auto&      form = a_state.situationForm;
			auto&      entry = form.entries[a_entry];
			const bool isDefault = a_entry == 0;
			Theme::LabelText("Name");
			ImGui::BeginDisabled(isDefault);
			ImGui::SetNextItemWidth(-FLT_MIN);
			Theme::InputText("##name", entry.name.data(), entry.name.size(), ImGuiInputTextFlags_None, kNeedName);
			if (ImGui::IsItemActivated()) {
				entry.nameBefore = entry.name.data();
				form.nameEditing = static_cast<int>(a_entry);
				form.nameField = ImGui::GetItemID();
			}
			const bool nameHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();
			// After EndDisabled, so the tooltip is not drawn faded.
			if (isDefault && nameHovered) {
				ImGui::SetTooltip("%s", kDefaultName);
			}
			Theme::LabelText("Description");
			ImGui::SetNextItemWidth(-FLT_MIN);
			Theme::InputText("##description", entry.description.data(), entry.description.size(), ImGuiInputTextFlags_None);
			const auto description = Trimmed(entry.description.data());
			DrawNote(std::format("Shown as: Dressed by Livery in \"Outfit\"{}.", description.empty() ? "" : ", " + description));
			Theme::LabelText("Conditions");
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", a_state.request.situations[a_entry].conditions.c_str());
			ImGui::PopStyleColor();
		}

		enum class DetailsAction
		{
			kNone,
			kShowList,
			kRemove
		};

		// The selected situation's details as edited: its name, description and conditions,
		// Random Outfit (for #{Name}), then Show Outfit List centred with Remove at the row's
		// right end (not for the Default).
		DetailsAction DrawSituationDetails(State& a_state)
		{
			constexpr const char* kShowList = "Show Outfit List";

			auto&       form = a_state.situationForm;
			const auto  selected = static_cast<std::size_t>(form.selected);
			auto&       entry = form.entries[selected];
			const auto& name = a_state.request.actorName;
			ImGui::PushID(form.selected);
			DrawGlobalSettings(a_state, selected);

			Theme::SectionGap();  // a field of its own, with no label
			bool              random = entry.row.random;
			const std::string randomLabel = std::format("Random Outfit (for {})", name);
			if (Theme::Checkbox(randomLabel.c_str(), &random)) {
				QueueMenuSound(kFocusSound);
				OutfitRows::SetRandom(entry.row, random);  // off keeps the last pick on
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				DrawHelpTooltip(std::format(
					"On: one outfit of the list is picked at random each time this situation begins for {}, and worn until it "
					"ends. Off: only the outfit shown is worn, the last one picked; the others stay on the list, hidden.",
					name));
			}

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kPaneButtonGap * Theme::Scale());
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - Theme::ButtonWidth(kShowList)) * 0.5f));
			auto action = Theme::Button(kShowList) ? DetailsAction::kShowList : DetailsAction::kNone;
			if (selected > 0 && Theme::LineLink("Remove")) {
				action = DetailsAction::kRemove;
			}
			ImGui::PopID();
			return action;
		}

		enum class OutfitViewAction
		{
			kNone,
			kEdit,
			kBack
		};

		// The rows the page's edits give outfit `a_outfit`, named as Manage Wardrobe's dropdown
		// names them (`Default, Town`), or `None`.
		std::string HeldRowsText(const State& a_state, std::uint32_t a_outfit)
		{
			std::string names;
			for (const auto& entry : a_state.situationForm.entries) {
				if (OutfitRows::Holds(entry.row, a_outfit)) {
					names += std::format("{}{}", names.empty() ? "" : ", ", Trimmed(entry.name.data()));
				}
			}
			return names.empty() ? "None" : names;
		}

		// The outfit clicked last, read only as Manage Wardrobe lays it out (DrawOutfitSummary),
		// its rows as the page's edits have them; then Edit and Back centred. Its pieces' list
		// ends where Edit and Back still end by the Situation List's bottom `a_listBottom`
		// (screen Y), so the view does not move the page's buttons, and scrolls when the pieces
		// need more. Escape is Back, and sets `a_escapeTaken` so it is not also Exit.
		// Contract: ShownPane is RightPane::kOutfit.
		OutfitViewAction DrawOutfitView(State& a_state, float a_listBottom, bool& a_escapeTaken)
		{
			const auto  id = a_state.situationForm.viewedOutfit;
			const float buttonRow = ImGui::GetStyle().ItemSpacing.y + kPaneButtonGap * Theme::Scale() + Theme::ButtonHeight();  // under the pieces' list
			ImGui::PushID("outfitView");
			DrawOutfitSummary(a_state, IndexOfOutfit(a_state, id), HeldRowsText(a_state, id), a_listBottom - buttonRow);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kPaneButtonGap * Theme::Scale());
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - ButtonRowWidth({ "Edit", "Back" })) * 0.5f));
			const bool edit = Theme::Button("Edit");
			ImGui::SameLine();
			const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			const bool back = Theme::Button("Back") || escape;
			ImGui::PopID();
			a_escapeTaken = a_escapeTaken || escape;
			if (edit) {
				return OutfitViewAction::kEdit;
			}
			return back ? OutfitViewAction::kBack : OutfitViewAction::kNone;
		}

		// --- Buttons, popups --------------------------------------------------

		enum class SituationAction
		{
			kNone,
			kRestore,
			kSave,
			kExit
		};

		// Save Changes, greyed until something changed, and Exit, centred as the other pages'
		// buttons, then Restore Presets (?) at the row's left end, a link as the details' Remove
		// and Restore Outfit are, greyed while every preset is as installed. a_escapeTaken: the
		// browser used this frame's Escape, which is otherwise Exit.
		SituationAction DrawSituationButtons(const State& a_state, bool a_escapeTaken)
		{
			constexpr const char* kRestoreHelp =
				"Brings the preset situations back as installed: a removed one returns, empty, and a renamed or "
				"redescribed one gets its words back. Nobody's outfits change.";

			const auto& request = a_state.request;
			const float left = ImGui::GetCursorPosX();
			BeginButtonRow({ "Save Changes", "Exit" });
			ImGui::BeginDisabled(!request.onSituationsSave);
			const bool save = ReasonButton("Save Changes", HasSituationEdits(a_state) ? nullptr : kNothingChanged);
			ImGui::EndDisabled();
			ImGui::SameLine();
			const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			const bool exit = Theme::Button("Exit") || escape;

			const bool installed = request.restoreChanges.empty();
			ImGui::BeginDisabled(installed || !request.onRestorePresets);
			bool       helpHovered = false;
			const bool restore = Theme::LineHelpLinkAt("Restore Presets", left, &helpHovered);
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();
			// After EndDisabled, so the tooltip is not drawn faded.
			if (installed && hovered) {
				ImGui::SetTooltip("%s", kAsInstalled);
			} else if (helpHovered) {
				DrawHelpTooltip(kRestoreHelp);
			}

			if (restore) {
				return SituationAction::kRestore;
			}
			if (save) {
				return SituationAction::kSave;
			}
			return exit ? SituationAction::kExit : SituationAction::kNone;
		}

		// Confirm and Cancel under a popup's text; Escape is Cancel. Returns true on Confirm.
		// Contract: inside a BeginModal popup.
		bool DrawPopupButtons(const char* a_confirm)
		{
			BeginButtonRow({ a_confirm, "Cancel" });
			const bool confirm = Theme::Button(a_confirm);
			ImGui::SameLine();
			const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
			EndButtonRow();
			if (confirm || cancel) {
				QueueMenuSound(confirm ? kOkSound : kCancelSound);
				ImGui::CloseCurrentPopup();
			}
			return confirm;
		}

		// `Remove "Town"?`, who has outfits on it in this save, what Restore Presets brings
		// back, and with unsaved edits that they are discarded; Remove / Cancel. Returns true on
		// Remove.
		bool DrawRemoveSituationPopup(const State& a_state)
		{
			if (!BeginModal(kRemoveSituationPopup)) {
				return false;
			}
			const int selected = a_state.situationForm.selected;
			if (selected <= 0) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return false;
			}
			const auto& saved = a_state.request.situations[static_cast<std::size_t>(selected)];
			ImGui::TextUnformatted(std::format("Remove \"{}\"?", saved.name).c_str());
			if (!saved.holders.empty()) {
				ImGui::TextUnformatted("It is used by:");
				for (const auto& holder : saved.holders) {
					ImGui::BulletText("%s", holder.c_str());
				}
				ImGui::TextUnformatted(std::format("Restore Presets brings \"{}\" back, without them.", saved.name).c_str());
			} else {
				ImGui::TextUnformatted(std::format("Restore Presets brings \"{}\" back.", saved.name).c_str());
			}
			if (HasSituationEdits(a_state)) {
				ImGui::TextUnformatted(kDiscardNote);
			}
			const bool remove = DrawPopupButtons("Remove");
			ImGui::EndPopup();
			return remove;
		}

		// What Restore Presets changes, one line per situation, and with unsaved edits that
		// they are discarded; Restore / Cancel. Returns true on Restore.
		bool DrawRestorePresetsPopup(const State& a_state)
		{
			if (!BeginModal(kRestorePresetsPopup)) {
				return false;
			}
			ImGui::TextUnformatted("Restore the preset situations?");
			for (const auto& line : a_state.request.restoreChanges) {
				ImGui::BulletText("%s", line.c_str());
			}
			if (HasSituationEdits(a_state)) {
				ImGui::TextUnformatted(kDiscardNote);
			}
			const bool restore = DrawPopupButtons("Restore");
			ImGui::EndPopup();
			return restore;
		}

		// Hands Save Changes' edits to onSituationsSave: the names and descriptions that
		// changed, every row when any changed, and the outfit whose details show, which stays on
		// the preview. Until the handler ends with SetOutfits or SetError the page is greyed.
		// Contract: the handler writes situations.json first and changes no row for a refused
		// or failed write (CLAUDE.md §2.1).
		void RequestSituationsSave(State& a_state)
		{
			const auto&                  form = a_state.situationForm;
			const auto&                  request = a_state.request;
			CaptureWindow::SituationSave save;
			for (std::size_t i = 0; i < form.entries.size(); ++i) {
				const auto& saved = request.situations[i];
				auto        name = Trimmed(form.entries[i].name.data());
				auto        description = Trimmed(form.entries[i].description.data());
				if (name != saved.name || description != saved.description) {
					save.edits.push_back({ .situation = saved.situation, .name = std::move(name), .description = std::move(description) });
				}
			}
			if (RowsChanged(a_state)) {
				for (std::size_t i = 0; i < form.entries.size(); ++i) {
					save.rows.push_back({ .situation = request.situations[i].situation, .row = form.entries[i].row });
				}
				save.dropDefaultPieces = request.assigned[0].pieces && !form.defaultPieces;
			}
			if (const int viewed = IndexOfOutfit(a_state, form.viewedOutfit); viewed >= 0) {
				save.previewPieces = OutfitAt(a_state, viewed).pieces;
			}
			a_state.status = Status::kSaving;
			a_state.error.clear();
			auto onSave = request.onSituationsSave;
			SKSE::GetTaskInterface()->AddTask([onSave, save = std::move(save)] { onSave(save); });
		}

		// Hands the selected situation to onSituationRemove; the page is greyed until the
		// handler ends with SetOutfits or SetError.
		// Contract: the handler writes situations.json first and changes no row for a refused
		// or failed write (CLAUDE.md §2.1).
		void RequestSituationRemove(State& a_state)
		{
			a_state.status = Status::kSaving;
			a_state.error.clear();
			auto onRemove = a_state.request.onSituationRemove;
			SKSE::GetTaskInterface()->AddTask(
				[onRemove, situation = a_state.request.situations[static_cast<std::size_t>(a_state.situationForm.selected)].situation] { onRemove(situation); });
		}

		void RequestRestorePresets(State& a_state)
		{
			a_state.status = Status::kSaving;
			a_state.error.clear();
			auto onRestore = a_state.request.onRestorePresets;
			SKSE::GetTaskInterface()->AddTask([onRestore] { onRestore(); });
		}

		// The page's popups, each opened by the page in its own ID scope. The unsaved-changes
		// alert's Save Changes saves as the button does, and the leave follows once the save
		// took (Draw); Discard Changes starts the edits again from the request.
		void DrawSituationPopups(State& a_state)
		{
			if (DrawRemoveSituationPopup(a_state)) {
				RequestSituationRemove(a_state);
			}
			if (DrawRestorePresetsPopup(a_state)) {
				RequestRestorePresets(a_state);
			}
			const char* noSave = a_state.request.onSituationsSave ? nullptr : kNothingChanged;
			switch (DrawLeavePopup(a_state, "You have unsaved changes.", noSave)) {
			case LeaveChoice::kSave:
				RequestSituationsSave(a_state);
				a_state.leaveAfterSave = true;
				break;
			case LeaveChoice::kDiscard:
				FillSituationForm(a_state, a_state.request.situations);
				Leave(a_state, *std::exchange(a_state.leave, std::nullopt));
				break;
			case LeaveChoice::kBack:
			case LeaveChoice::kNone:
				break;
			}
		}
	}

	void FillSituationForm(State& a_state, const std::vector<CaptureWindow::SituationInfo>& a_before)
	{
		auto&       form = a_state.situationForm;
		const auto& request = a_state.request;
		const auto  keyOf = [&a_before](int a_index) -> std::optional<Situations::Situation> {
			if (a_index < 0 || static_cast<std::size_t>(a_index) >= a_before.size()) {
				return std::nullopt;
			}
			return a_before[static_cast<std::size_t>(a_index)].situation;
		};
		const auto selected = keyOf(form.selected);

		std::vector<SituationEntry> entries(request.situations.size());
		form.selected = -1;
		for (std::size_t i = 0; i < entries.size(); ++i) {
			const auto& info = request.situations[i];
			auto&       entry = entries[i];
			CopyToField(entry.name, info.name);
			CopyToField(entry.description, info.description);
			entry.row = request.assigned[i].row;
			for (std::size_t j = 0; j < a_before.size() && j < form.entries.size(); ++j) {
				if (a_before[j].situation == info.situation) {
					entry.folded = form.entries[j].folded;
				}
			}
			if (selected && info.situation == *selected) {
				form.selected = static_cast<int>(i);
			}
		}
		form.entries = std::move(entries);
		form.defaultPieces = !request.assigned.empty() && request.assigned[0].pieces;
		form.details = form.details && form.selected >= 0;
		form.nameEditing = -1;
		form.dragHover = -1;
	}

	bool HasSituationEdits(const State& a_state)
	{
		return WordsChanged(a_state) || RowsChanged(a_state);
	}

	void OfferOutfitDrag(const OutfitPayload& a_payload, const char* a_label)
	{
		if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
			return;
		}
		ImGui::SetDragDropPayload(kOutfitPayload, &a_payload, sizeof(a_payload));
		ImGui::TextUnformatted(a_label);
		ImGui::EndDragDropSource();
	}

	// The Situation List on the left, the outfit list or the selected situation on the
	// right; every change is an edit until Save Changes, but Remove and Restore Presets, which
	// write at once after their popups. The preview shows the Inventory page's
	// ticks throughout.
	void DrawSituationPage(State& a_state)
	{
		auto& form = a_state.situationForm;
		if (std::exchange(a_state.openLeave, false)) {
			ImGui::OpenPopup(kLeavePopup);
		}
		EndInactiveNameEdit(form);
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		bool escapeTaken = false;
		auto details = DetailsAction::kNone;
		auto outfitView = OutfitViewAction::kNone;
		if (BeginColumns("situations")) {
			ImGui::TableNextColumn();
			const float listBottom = DrawSituationList(a_state);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			switch (ShownPane(a_state)) {
			case RightPane::kOutfit:
				outfitView = DrawOutfitView(a_state, listBottom, escapeTaken);
				break;
			case RightPane::kSituation:
				details = DrawSituationDetails(a_state);
				break;
			case RightPane::kOutfitList:
				escapeTaken = DrawOutfitPane(a_state);
				break;
			}
			EndColumns();
		}
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		const auto action = DrawSituationButtons(a_state, escapeTaken);
		EndButtonRow();
		ImGui::EndDisabled();

		if (outfitView == OutfitViewAction::kBack) {
			QueueMenuSound(kCancelSound);
			CloseOutfitView(a_state);
		} else if (outfitView == OutfitViewAction::kEdit) {
			QueueMenuSound(kOkSound);
			RequestLeave(a_state, { .page = Page::kManage, .manageOutfit = form.viewedOutfit });
		}
		if (details == DetailsAction::kShowList) {
			QueueMenuSound(kFocusSound);
			form.details = false;
			form.entries[static_cast<std::size_t>(form.selected)].folded = false;
			form.scrollToSelected = true;
		} else if (details == DetailsAction::kRemove) {
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kRemoveSituationPopup);
		}
		switch (action) {
		case SituationAction::kRestore:
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kRestorePresetsPopup);
			break;
		case SituationAction::kSave:
			QueueMenuSound(kOkSound);
			RequestSituationsSave(a_state);
			break;
		case SituationAction::kExit:
			RequestLeave(a_state, { .exit = true });
			break;
		case SituationAction::kNone:
			break;
		}
		DrawSituationPopups(a_state);
	}
}
