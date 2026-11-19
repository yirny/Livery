// SPDX-License-Identifier: GPL-3.0-or-later
//
// The capture window's the Situations page (docs/situation-management-ui.md §3-§6; see
// CaptureWindow.cpp for the window, CaptureWindowInternal.h for the state the pages share).
//
// FILE MAP
//   Constants       - the drag's open delay, the page's popups, texts and reasons
//   Edits           - the form from the request, what differs from it, a name's rule at the
//                     end of its edit
//   Drag and drop   - an outfit dragged from either pane, dropped on a situation (at its front,
//                     or between two of its outfits; moved with the items it leaves off) or on
//                     the outfit list (taken off its row); a folded heading opens once a drag
//                     has hovered it a while; a situation dropped between two others (the order)
//   Never worn      - which situations one lower in the list covers (SituationRules::CoveredBy)
//   Situation List  - the left pane: the Default's heading, the order's caption, then a heading
//                     per situation in the order (Edit opens the Edit situation popup, its chevron
//                     folds, the die of the row worn puts on another of its outfits) over the
//                     outfits it shows; a click on an outfit row of either pane shows its details
//                     and puts it on the preview
//   Condition editor - a situation's conditions (docs/custom-situations.md §7): per condition a
//                     heading with its Remove and a row that reads as a sentence (whose state, "is" or
//                     a two-state dropdown, an operator dropdown and its values), places or spells
//                     under it with a × each, the one clicked in the Applies text lit; Add
//                     Condition..., Allow during combat
//   Right pane      - the outfit list with what a drag does under it; or an outfit's details with
//                     Open in Wardrobe and Back, its pieces ticked as the row it was clicked under
//                     wears them
//   Buttons, popups - New Situation..., Exit, Restore Presets (?); the New situation, Remove's and
//                     Restore Presets' popups, each handing its write to the Request's handler;
//                     the Edit situation popup (name, never worn, the Applies text with a link per
//                     condition, Random Outfit (for #{Name}); the editor beside them; Done with
//                     Remove Situation) with the Add place and Add spell popups over it, edits of the
//                     selected situation's Place or Spell row
//   Saving          - an edit is saved shortly after the last one, as the page is left, or with
//                     a die's click (ScheduleAutosave, SaveSituationEdits, RequestReroll)
#include "PCH.h"

#include "CaptureWindowInternal.h"

#include "OutfitBrowser.h"
#include "OutfitRows.h"
#include "Utils.h"
#include "sarrai/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>  // BeginDragDropTargetCustom, ImRect, GetActiveID

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CaptureWindow::Internal
{
	namespace
	{
		// --- Constants --------------------------------------------------------

		// How long a drag hovers a folded heading before it opens (seconds).
		constexpr double      kDragExpandDelay = 1.0;
		constexpr const char* kRemoveSituationPopup = "Remove situation?";
		constexpr const char* kRestorePresetsPopup = "Restore presets?";
		constexpr const char* kNewSituationPopup = "New situation";
		constexpr const char* kAddPlacePopup = "Add location";
		constexpr const char* kAddSpellPopup = "Add spell";
		constexpr const char* kEditSituationPopup = "Edit situation";
		constexpr const char* kNewSituation = "New Situation...";
		// Over a condition in the Applies text: whether it is met now (CheckConditions), and while
		// the settings page does not offer the editor, how to offer it.
		constexpr const char* kMetNow = "Met now.";
		constexpr const char* kNotMetNow = "Not met now.";
		constexpr const char* kTurnOnCustom = "Turn on custom situations in the settings to edit.";
		// How often the Edit situation popup asks whether its conditions are met (seconds): the
		// weather, the time and the actor change under it.
		constexpr double kHoldsRefresh = 0.5;
		// A condition's box in the editor, in every skin (pixels at 1080p): its padding inside,
		// the gap under it, its corners' radius.
		constexpr ImVec2 kConditionBoxPadding{ 12.0f, 8.0f };
		constexpr float  kConditionBoxGap = 8.0f;
		constexpr float  kConditionBoxRounding = 4.0f;
		// Reasons a button is greyed out, shown on hover.
		constexpr const char* kAsInstalled = "Every preset situation is as installed.";
		// The name a situation gets when neither the one typed nor the one before can be used.
		constexpr const char* kUnnamed = "Unnamed Situation";
		// A line in an outfit row's place under a heading.
		constexpr const char* kPiecesLine = "Items not in the Wardrobe";
		// The widest time a Time of day dropdown shows, for its width.
		constexpr const char* kWidestTime = "12:30 AM";
		// A situation dragged by its heading's grip to another place in the order.
		constexpr const char* kSituationPayload = "LiverySituation";
		// Under the Default's heading, over the order.
		constexpr const char* kOrderCaption = "The lowest that applies is worn.";
		// The help of `Applies` and of the editor's `Conditions`: one text, read in either column.
		constexpr const char* kConditionsHelp = "Applies while every condition is met.\nA condition with several values is met by any of them.";

		// --- Edits ------------------------------------------------------------

		void CopyToField(std::array<char, kNameCapacity>& a_field, const std::string& a_text)
		{
			a_field.fill('\0');
			std::strncpy(a_field.data(), a_text.c_str(), kNameCapacity - 1);
		}

		// A situation's name differs from situations.json.
		bool NamesChanged(const State& a_state)
		{
			const auto& entries = a_state.situationForm.entries;
			for (std::size_t i = 0; i < entries.size(); ++i) {
				if (Trimmed(entries[i].name.data()) != a_state.request.situations[i].name) {
					return true;
				}
			}
			return false;
		}

		// Entry `a_index`'s conditions differ from situations.json; the Default has none.
		bool RulesChanged(const State& a_state, std::size_t a_index)
		{
			return a_index > 0 && a_state.situationForm.entries[a_index].rules != a_state.request.situations[a_index].rules;
		}

		// The Situation List's order differs from situations.json's, request.situations' own.
		bool OrderChanged(const SituationForm& a_form)
		{
			for (std::size_t i = 0; i < a_form.order.size(); ++i) {
				if (a_form.order[i] != i + 1) {
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

		// The name being typed, if any, ends its edit before a save reads the names.
		void EndNameEditing(SituationForm& a_form)
		{
			if (a_form.nameEditing >= 0 && static_cast<std::size_t>(a_form.nameEditing) < a_form.entries.size()) {
				EndNameEdit(a_form, static_cast<std::size_t>(a_form.nameEditing));
			}
			a_form.nameEditing = -1;
		}

		// A name field that is no longer active ends its edit, before anything this frame reads
		// the name; it may have gone from view, its situation unselected. The name so entered
		// is saved as any edit is.
		void EndInactiveNameEdit(State& a_state)
		{
			auto& form = a_state.situationForm;
			if (form.nameEditing >= 0 && ImGui::GetActiveID() != form.nameField) {
				if (static_cast<std::size_t>(form.nameEditing) < form.entries.size()) {
					EndNameEdit(form, static_cast<std::size_t>(form.nameEditing));
					ScheduleAutosave(a_state);
				}
				form.nameEditing = -1;
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

		// A drop changed the rows: the preview follows the outfit whose details show when what it
		// wears changed (`a_before`, ViewedOutfitPieces before the drop).
		void FollowViewedPieces(const State& a_state, const std::vector<std::size_t>& a_before)
		{
			if (a_state.situationForm.viewedOutfit == 0) {
				return;
			}
			if (auto now = ViewedOutfitPieces(a_state); now != a_before) {
				QueuePreview(a_state, std::move(now));
			}
		}

		// An outfit dropped on situation `a_target`, before what it shows at `a_index` (the
		// front with random off, where only `chosen` shows): from the outfit list a copy, all its
		// items worn; from a situation moved, off its row first (its `chosen` 0 if it was that)
		// with the items it left off there, which it leaves off on the new row unless that has
		// it already, its details following it; or within its own row reordered. An outfit made
		// the Default's `chosen` drops its pieces, as Assignments does.
		void DropOnSituation(State& a_state, std::size_t a_target, std::size_t a_index, const OutfitPayload& a_payload)
		{
			auto&                      form = a_state.situationForm;
			const auto                 before = ViewedOutfitPieces(a_state);
			const bool                 moved = a_payload.from >= 0 && static_cast<std::size_t>(a_payload.from) != a_target;
			std::vector<std::uint32_t> carried;
			if (moved) {
				auto& source = form.entries[static_cast<std::size_t>(a_payload.from)].row;
				carried = OutfitRows::ExcludedOf(source, a_payload.outfit);
				OutfitRows::Take(source, a_payload.outfit);
			}
			auto&      row = form.entries[a_target].row;
			const bool arrives = std::ranges::find(row.outfits, a_payload.outfit) == row.outfits.end();
			OutfitRows::Place(row, a_payload.outfit, row.random ? a_index : 0);
			if (moved && arrives) {
				OutfitRows::SetExcluded(row, a_payload.outfit, std::move(carried));
			}
			if (moved && form.viewedOutfit == a_payload.outfit && form.viewedRow == a_payload.from) {
				form.viewedRow = static_cast<int>(a_target);
			}
			if (a_target == 0 && !row.random) {
				form.defaultPieces = false;
			}
			FollowViewedPieces(a_state, before);
			ScheduleAutosave(a_state);
		}

		// A situation dragged by its grip: the entry it is.
		struct SituationPayload
		{
			std::size_t entry = 0;
		};

		// The situation the drag on now carries; nullptr for none.
		const SituationPayload* DraggedSituation()
		{
			const auto* payload = ImGui::GetDragDropPayload();
			return payload && payload->IsDataType(kSituationPayload) ? static_cast<const SituationPayload*>(payload->Data) : nullptr;
		}

		// Accepts a situation dragged over the drop target just begun, with the line at `a_lineY`
		// while it hovers, and ends the target. Returns the dragged entry once dropped.
		// Contract: right after a BeginDragDropTarget that returned true.
		std::optional<std::size_t> AcceptSituation(float a_lineY)
		{
			std::optional<std::size_t> dropped;
			if (DraggedSituation()) {
				if (const auto* payload = ImGui::AcceptDragDropPayload(kSituationPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
					DrawInsertLine(a_lineY);
					if (payload->IsDelivery()) {
						dropped = static_cast<const SituationPayload*>(payload->Data)->entry;
					}
				}
			}
			ImGui::EndDragDropTarget();
			return dropped;
		}

		// Situation `a_entry` dropped before the one at `a_place` in the order (its end with
		// order.size()). Nobody is redressed for it: the poll follows the order once the window
		// closes.
		void DropSituation(State& a_state, std::size_t a_entry, std::size_t a_place)
		{
			auto&      order = a_state.situationForm.order;
			const auto from = std::ranges::find(order, a_entry);
			if (from == order.end()) {
				return;
			}
			const auto index = static_cast<std::size_t>(from - order.begin());
			const auto place = a_place > index ? a_place - 1 : a_place;  // the place once it is out
			if (place == index) {
				return;
			}
			order.erase(from);
			order.insert(order.begin() + static_cast<std::ptrdiff_t>(place), a_entry);
			a_state.situationForm.neverWornStale = true;
			ScheduleAutosave(a_state);
		}

		// Situation `a_entry`'s drag from anywhere on its heading (Theme::SituationHeadingLook::grip),
		// a see-through copy of the heading following the mouse (Theme::SituationDragGhost).
		// Contract: right after the SituationHeading it drags, whose part held is the last item.
		void OfferSituationDrag(std::size_t a_entry, const char* a_name)
		{
			if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
				return;
			}
			const SituationPayload payload{ .entry = a_entry };
			ImGui::SetDragDropPayload(kSituationPayload, &payload, sizeof(payload));
			Theme::SituationDragGhost(a_name);
			ImGui::EndDragDropSource();
		}

		// A drag from the Situation List dropped on the outfit list: the outfit leaves its row
		// (its `chosen` 0 if it was that), or the Default's pieces go.
		void DropOnOutfitList(State& a_state, const OutfitPayload& a_payload)
		{
			auto& form = a_state.situationForm;
			if (a_payload.from < 0) {
				return;
			}
			ScheduleAutosave(a_state);
			if (a_payload.outfit == 0) {
				form.defaultPieces = false;
				return;
			}
			const auto before = ViewedOutfitPieces(a_state);
			OutfitRows::Take(form.entries[static_cast<std::size_t>(a_payload.from)].row, a_payload.outfit);
			FollowViewedPieces(a_state, before);
		}

		// --- Never worn -------------------------------------------------------

		// For each situation, those lower in the list that apply whenever it does
		// (SituationRules::CoveredBy, from the rules and the order as edited), nearest first. At
		// most kMaxSituations squared pairs, each a few rows.
		void TellNeverWorn(State& a_state)
		{
			auto& form = a_state.situationForm;
			form.neverWornBy.assign(form.entries.size(), {});
			for (std::size_t p = 0; p < form.order.size(); ++p) {
				const auto& rules = form.entries[form.order[p]].rules;
				for (std::size_t q = p + 1; q < form.order.size(); ++q) {
					if (SituationRules::CoveredBy(rules, form.entries[form.order[q]].rules, a_state.request.placeFacts)) {
						form.neverWornBy[form.order[p]].push_back(form.order[q]);
					}
				}
			}
			form.neverWornStale = false;
		}

		// The nearest entry lower in the list that applies whenever entry `a_entry` does, so that
		// one is never worn; none while it may be worn.
		std::optional<std::size_t> NeverWornBy(const SituationForm& a_form, std::size_t a_entry)
		{
			if (a_entry >= a_form.neverWornBy.size() || a_form.neverWornBy[a_entry].empty()) {
				return std::nullopt;
			}
			return a_form.neverWornBy[a_entry].front();
		}

		// "Never worn: Rainy town, lower in the list, applies whenever this does." and how to
		// change that, naming entry `a_by` (NeverWornBy) after its star in the emphasis, each
		// sentence wrapped (Theme::RunTextWrapped) in the text colour pushed now; its lines as
		// close as a wrapped text's.
		void DrawNeverWornText(const SituationForm& a_form, std::size_t a_by)
		{
			const auto                              name = Trimmed(a_form.entries[a_by].name.data());
			const std::array<Theme::SubtitleRun, 3> why{ { TextRun("Never worn: "), SituationRun(name.c_str()), TextRun(", lower in the list, applies whenever this does.") } };
			const std::array<Theme::SubtitleRun, 3> fix{ { TextRun("To change that, add a condition, edit "), SituationRun(name.c_str()),
				TextRun(", or move one of them in the list.") } };
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
			Theme::RunTextWrapped(why);
			ImGui::PopStyleVar();
			Theme::RunTextWrapped(fix);
		}

		// Entry `a_entry` applies whenever entry `a_of` (-1 for none) does, so `a_of` is never
		// worn: its heading's name shows in the error colour while `a_of` is selected or its
		// never-worn mark hovered.
		bool Overrides(const SituationForm& a_form, int a_of, std::size_t a_entry)
		{
			if (a_of <= 0 || static_cast<std::size_t>(a_of) >= a_form.neverWornBy.size()) {
				return false;
			}
			const auto& by = a_form.neverWornBy[static_cast<std::size_t>(a_of)];
			return std::ranges::find(by, a_entry) != by.end();
		}

		// --- Situation List ---------------------------------------------------

		// Back from an outfit's details: the right pane shows what it showed before them, and
		// the preview the Dress page's ticks again.
		void CloseOutfitView(State& a_state)
		{
			a_state.situationForm.viewedOutfit = 0;
			a_state.situationForm.viewedRow = -1;
			QueuePreview(a_state, TickedPieces(a_state));
		}

		// A click on an outfit row of either pane, under situation `a_row`'s heading or on the
		// outfit list (-1): its details on the right and the outfit, less the items that row
		// leaves off, on the preview; on the row whose details show, back, as Back does.
		void ViewOutfit(State& a_state, int a_index, int a_row)
		{
			auto&       form = a_state.situationForm;
			const auto& outfit = OutfitAt(a_state, a_index);
			if (form.viewedOutfit == outfit.id && form.viewedRow == a_row) {
				CloseOutfitView(a_state);
				return;
			}
			form.viewedOutfit = outfit.id;
			form.viewedRow = a_row;
			QueuePreview(a_state, ViewedOutfitPieces(a_state));
		}

		// The entry whose row the ticks of the shown outfit's pieces edit: the one it was clicked
		// under, while that row holds it (OutfitRows::Holds); -1 for none, the pieces read only.
		int EditedRow(const SituationForm& a_form)
		{
			const auto row = a_form.viewedRow;
			if (row < 0 || static_cast<std::size_t>(row) >= a_form.entries.size()) {
				return -1;
			}
			return OutfitRows::Holds(a_form.entries[static_cast<std::size_t>(row)].row, a_form.viewedOutfit) ? row : -1;
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
			return a_entry == 0 && a_form.defaultPieces && !OutfitRows::HoldsAny(a_form.entries[0].row);
		}

		// Something shows under situation `a_entry`'s heading: an outfit, or the Default's
		// pieces. One with nothing wears nothing: its heading has a greyed name.
		bool ShowsAnything(const SituationForm& a_form, std::size_t a_entry)
		{
			return OutfitRows::HoldsAny(a_form.entries[a_entry].row) || ShowsPieces(a_form, a_entry);
		}

		// A grey line in an outfit row's place (kPiecesLine), with no icon.
		void DrawLineRow(const char* a_text, Theme::Grip a_grip)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			Theme::OutfitRow(a_text, false, {}, false, a_grip, nullptr);
			ImGui::PopStyleColor();
		}

		// The Situation List's height with every heading open, capped as ListHeight caps it, and
		// at least to the bottom of the right pane with the outfit list, `a_top` (pixels) below
		// the pane's top, so neither folding nor the right pane moves the buttons. An outfit's
		// details may be taller, moving them (DrawOutfitView).
		// Contract: with no item spacing pushed.
		float SituationListHeight(const State& a_state, float a_top)
		{
			const auto& form = a_state.situationForm;
			float       rows = form.entries.size() > 1 ? Theme::ListHeadingHeight() : 0.0f;  // the order's caption
			for (std::size_t i = 0; i < form.entries.size(); ++i) {
				rows += Theme::SituationHeadingHeight();
				if (ShowsPieces(form, i)) {
					rows += Theme::OutfitRowHeight();
				}
				rows += static_cast<float>(ShownOutfits(form.entries[i].row).size()) * Theme::OutfitRowHeight();
			}
			return (std::max)(FittedListHeight(rows, Theme::SituationHeadingHeight()), form.outfitPaneHeight - a_top);
		}

		// The outfits situation `a_entry` shows, each with a grip, dragged to move it, and a drop
		// place: before it on its upper half, after it on its lower half (random on; the front
		// with it off). With none, the Default's pieces line, which drags to the outfit list and
		// takes a drop at the front, or nothing.
		void DrawShownOutfits(State& a_state, std::size_t a_entry)
		{
			auto&      form = a_state.situationForm;
			const auto shown = ShownOutfits(form.entries[a_entry].row);
			if (shown.empty()) {
				if (!ShowsPieces(form, a_entry)) {
					return;
				}
				const float top = ImGui::GetCursorScreenPos().y;
				DrawLineRow(kPiecesLine, Theme::Grip::kLine);
				// The copy greyed as the line is.
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				OfferOutfitDrag({ .from = 0 }, kPiecesLine, Theme::Grip::kLine, nullptr);
				ImGui::PopStyleColor();
				if (ImGui::BeginDragDropTarget()) {
					if (const auto dropped = AcceptOnSituation(top)) {
						DropOnSituation(a_state, a_entry, 0, *dropped);
					}
				}
				return;
			}
			const bool random = form.entries[a_entry].row.random;
			for (std::size_t k = 0; k < shown.size(); ++k) {
				// Contract: every outfit on the rows is on the window's list: AssignedRows (Plugin.cpp)
				// leaves out those gone from the library, and SetOutfits refills the rows with the list.
				const int index = IndexOfOutfit(a_state, shown[k]);
				if (index < 0) {
					continue;
				}
				ImGui::PushID(static_cast<int>(k));
				const float top = ImGui::GetCursorScreenPos().y;
				// No star: every row here is one chosen for a situation.
				const bool viewed = form.viewedOutfit == shown[k] && form.viewedRow == static_cast<int>(a_entry);
				const auto excluded = OutfitRows::ExcludedOf(form.entries[a_entry].row, shown[k]);
				if (DrawOutfitRow(a_state, a_state.situationBrowser, index, viewed, k % 2 == 1, Theme::Grip::kName, false, &excluded)) {
					QueueMenuSound(kFocusSound);
					ViewOutfit(a_state, index, static_cast<int>(a_entry));
				}
				const float bottom = ImGui::GetItemRectMax().y;
				const auto& outfit = OutfitAt(a_state, index);
				OfferOutfitDrag({ .from = static_cast<int>(a_entry), .outfit = shown[k] }, outfit.name.c_str(), Theme::Grip::kName, &outfit.icon);
				if (ImGui::BeginDragDropTarget()) {
					const bool after = random && ImGui::GetMousePos().y >= (top + bottom) * 0.5f;
					if (const auto dropped = AcceptOnSituation(after ? bottom : top)) {
						DropOnSituation(a_state, a_entry, after ? k + 1 : k, *dropped);
					}
				}
				ImGui::PopID();
			}
		}

		// The die of situation `a_entry`'s heading, random on: a button while the actor wears an
		// outfit of its list (as edited) for it and another is on the list, with what a click
		// does on hover; else the reason it is not, on hover.
		struct Dice
		{
			bool        button = false;
			std::string tip;
		};

		Dice DiceOf(const State& a_state, std::size_t a_entry)
		{
			constexpr const char* kRandomLine = "One of these outfits is picked at random each time this situation begins.";

			const auto& request = a_state.request;
			const auto& outfits = a_state.situationForm.entries[a_entry].row.outfits;
			const bool  wears = request.wornOutfit != 0 && request.situations[a_entry].situation == request.wornFor &&
			                    std::ranges::find(outfits, request.wornOutfit) != outfits.end();
			if (!wears) {
				return { .tip = std::format("{}\n{} is not wearing one of them now.", kRandomLine, request.actorName) };
			}
			if (outfits.size() < 2) {
				return { .tip = std::format("{}\nAdd another to pick from.", kRandomLine) };
			}
			return { .button = true, .tip = std::format("{}\nClick to pick another now.", kRandomLine) };
		}

		// Situation `a_entry`'s heading, then, unless folded, what it shows. Edit asks for its Edit
		// situation popup, the chevron folds it, and a click elsewhere on it does nothing; it is
		// drawn selected while the popup is open on it. Every heading
		// but the Default's drags from anywhere on it to another place in the order while the page
		// offers that (Request::customSituations). A heading that is never worn carries a warning
		// mark after its name, saying why on hover (DrawNeverWornText); one whose situation makes
		// the one the popup is open on, or the one whose mark is hovered, never worn has its name in the error
		// colour (Overrides): those are lower in the list, drawn after the mark. Its die and
		// count show with random on alone; the die is a button as DiceOf says. Only random on
		// with something to show has the chevron; else it is never folded (random off shows its
		// one outfit, a drop shows what it got). With nothing to show its name is greyed. A drop
		// on the heading, its rule included, adds at the front. `a_dragHovered` becomes the entry
		// while a drag hovers its folded heading, which opens once DrawSituationList has seen
		// that for kDragExpandDelay. Returns HeadingClick::kDice or kEdit when its die or Edit was
		// clicked, else kNone.
		Theme::HeadingClick DrawSituationEntry(State& a_state, std::size_t a_entry, int& a_dragHovered)
		{
			auto&      form = a_state.situationForm;
			auto&      entry = form.entries[a_entry];
			const int  index = static_cast<int>(a_entry);
			const int  editedNow = form.editing ? form.selected : -1;
			const bool shows = ShowsAnything(form, a_entry);
			const bool foldable = shows && entry.row.random;
			if (!foldable) {
				entry.folded = false;
			}
			const auto  dice = entry.row.random ? DiceOf(a_state, a_entry) : Dice{};
			const auto  neverWornBy = NeverWornBy(form, a_entry);
			const bool  grip = a_entry != 0 && a_state.request.customSituations;
			const float top = ImGui::GetCursorScreenPos().y;
			bool        warningHovered = false;
			const auto  click = Theme::SituationHeading(entry.name.data(),
				{ .count = entry.row.random ? static_cast<int>(entry.row.outfits.size()) : -1,
					.random = entry.row.random,
					.diceButton = dice.button,
					.diceTip = dice.tip.empty() ? nullptr : dice.tip.c_str(),
					.open = !entry.folded,
					.foldable = foldable,
					.selected = editedNow == index,
					.dim = !shows,
					.icon = a_entry == 0 ? Theme::HeadingIcon::kNone : Theme::HeadingIcon::kStar,  // the Default is no situation: no star
					.grip = grip,
					.alert = Overrides(form, editedNow, a_entry) || Overrides(form, form.warningHovered, a_entry),
					.warning = neverWornBy.has_value(),
					.warningHovered = &warningHovered });
			if (warningHovered) {
				form.warningHovered = index;
				BeginHelpTooltip();
				DrawNeverWornText(form, *neverWornBy);
				EndHelpTooltip();
			}
			if (grip) {
				OfferSituationDrag(a_entry, entry.name.data());
			}
			const float bottom = top + Theme::SituationHeadingHeight();
			if (form.scrollToSelected && form.selected == index) {
				ImGui::SetScrollHereY(0.0f);
				form.scrollToSelected = false;
			}
			if (click == Theme::HeadingClick::kFold) {
				QueueMenuSound(kFocusSound);
				entry.folded = !entry.folded;
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
					DropOnSituation(a_state, a_entry, 0, *dropped);
				}
			}
			if (!entry.folded) {
				DrawShownOutfits(a_state, a_entry);
			}
			return click == Theme::HeadingClick::kDice || click == Theme::HeadingClick::kEdit ? click : Theme::HeadingClick::kNone;
		}

		// The help of the Situation List's label: what the list is, which situation is worn; the
		// order's drag only while the page offers it.
		std::string SituationListHelp(const State& a_state)
		{
			const auto& request = a_state.request;
			return std::format(
				"Each situation lists the outfits {} can wear in it.\nThe lowest applicable situation takes priority. Otherwise, the "
				"default outfit is worn.{}",
				request.actorName, request.customSituations ? "\nDrag a situation by its grip to reorder it." : "");
		}

		// The help of the Outfit List's label: what a drag of an outfit does, and a click.
		constexpr const char* kOutfitListHelp =
			"Drag an outfit onto a situation to add it, or between outfits to set its position.\nDrag it to another situation to "
			"move it, or back here to remove it.\nClick an outfit to see its items.";

		// The situation at place `a_place` of the order, its heading and what it shows: a drop of
		// a situation over its heading's upper half goes before it, anywhere lower in its block
		// after it. Returns what DrawSituationEntry returns.
		Theme::HeadingClick DrawOrderedEntry(State& a_state, std::size_t a_place, int& a_dragHovered)
		{
			const auto  entry = a_state.situationForm.order[a_place];
			const float top = ImGui::GetCursorScreenPos().y;
			ImGui::PushID(static_cast<int>(entry));
			const auto   click = DrawSituationEntry(a_state, entry, a_dragHovered);
			const float  bottom = ImGui::GetCursorScreenPos().y;
			const float  left = ImGui::GetWindowPos().x;
			const ImRect block(ImVec2(left, top), ImVec2(left + ImGui::GetWindowWidth(), bottom));
			if (DraggedSituation() && ImGui::BeginDragDropTargetCustom(block, ImGui::GetID("situationDrop"))) {
				const bool before = ImGui::GetMousePos().y < top + Theme::SituationHeadingHeight() * 0.5f;
				if (const auto dropped = AcceptSituation(before ? top : bottom)) {
					DropSituation(a_state, *dropped, before ? a_place : a_place + 1);
				}
			}
			ImGui::PopID();
			return click;
		}

		// What a click in the Situation List asks of the page.
		struct ListClick
		{
			bool dice = false;  // a heading's die: another outfit of the row worn
			int  edit = -1;     // a heading's Edit: the entry to open the Edit situation popup on; -1 for none
		};

		// The left pane: the Situation List (?) label over the Default's heading, the order's
		// caption, then a heading per situation in the order.
		ListClick DrawSituationList(State& a_state)
		{
			auto& form = a_state.situationForm;
			if (form.neverWornStale) {
				TellNeverWorn(a_state);
			}
			form.warningHovered = -1;  // set again by its heading, before those it names
			const float paneTop = ImGui::GetCursorPosY();
			Theme::LabelText("Situation List");
			const bool helpHovered = Theme::LabelHelpMark();
			if (FocusStandIn("##listHelp") || helpHovered) {
				DrawHelpTooltip(SituationListHelp(a_state));
			}
			const float top = ImGui::GetCursorPosY() - paneTop;
			// No gap between rows: the headings and rows hold it themselves (Theme::OutfitRow).
			// Popped before EndList, as the outfit list's spacing is.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
			int        dragHovered = -1;
			ListClick  result;
			const auto take = [&result](Theme::HeadingClick a_click, std::size_t a_entry) {
				if (a_click == Theme::HeadingClick::kDice) {
					result.dice = true;
				} else if (a_click == Theme::HeadingClick::kEdit) {
					result.edit = static_cast<int>(a_entry);
				}
			};
			if (Theme::BeginList("situations", SituationListHeight(a_state, top)) && !form.entries.empty()) {
				ImGui::PushID(0);
				take(DrawSituationEntry(a_state, 0, dragHovered), 0);
				ImGui::PopID();
				if (!form.order.empty()) {
					// The caption takes a situation dropped on it first in the order.
					const float captionTop = ImGui::GetCursorScreenPos().y;
					Theme::ListHeading(kOrderCaption, Theme::HeadingIcon::kNone, true);  // a plain rule: no situation's heading
					const float  captionBottom = ImGui::GetCursorScreenPos().y;
					const float  left = ImGui::GetWindowPos().x;
					const ImRect caption(ImVec2(left, captionTop), ImVec2(left + ImGui::GetWindowWidth(), captionBottom));
					if (DraggedSituation() && ImGui::BeginDragDropTargetCustom(caption, ImGui::GetID("captionDrop"))) {
						if (const auto dropped = AcceptSituation(captionBottom)) {
							DropSituation(a_state, *dropped, 0);
						}
					}
				}
				for (std::size_t p = 0; p < form.order.size(); ++p) {
					take(DrawOrderedEntry(a_state, p, dragHovered), form.order[p]);
				}
			}
			ImGui::PopStyleVar();
			Theme::EndList();
			if (form.dragHover != dragHovered) {
				form.dragHover = dragHovered;
				form.dragHoverSince = ImGui::GetTime();
			}
			return result;
		}

		// --- Condition editor -------------------------------------------------
		//
		// Each condition has a heading, its kind's name with (?) and Remove at the line's right end,
		// and under it reads as a sentence (docs/custom-situations.md §7): a kind reading an actor's
		// state starts with whose, the wearer or the player (a dropdown), then "is" / "is not" and
		// its phrase, or for Weapon a dropdown of its two
		// states, as Indoors is; the others an operator dropdown ("is" / "is not", "is any of" /
		// "is none of" with several selected, "between" / "not between") and their values: a
		// multi-choice dropdown (Swimming's a single choice), the two times, or the places or
		// spells listed under, each with a ×.

		// The operator a row with values or forms reads with: "is" / "is not" up to one
		// selected, "is any of" / "is none of" with more; a Spell Effect row "has" / "has not",
		// "has any of" / "has none of".
		const char* OperatorLabel(const SituationRules::Row& a_row, bool a_negated)
		{
			const auto count = SituationRules::HasValues(a_row.kind) ? static_cast<std::size_t>(std::popcount(a_row.values)) : SituationRules::FormsOf(a_row).size();
			if (a_row.kind == SituationRules::Kind::kSpell) {
				if (count > 1) {
					return a_negated ? "has none of" : "has any of";
				}
				return a_negated ? "has not" : "has";
			}
			if (count > 1) {
				return a_negated ? "is none of" : "is any of";
			}
			return a_negated ? "is not" : "is";
		}

		// The widest of `a_labels` as a dropdown's preview and row (Theme::DropdownWidth).
		float WidestDropdown(std::initializer_list<const char*> a_labels)
		{
			const Theme::FieldStyle style{ .filled = true };
			float                   widest = 0.0f;
			for (const char* label : a_labels) {
				widest = (std::max)(widest, Theme::DropdownWidth(label, style));
			}
			return widest;
		}

		// A row's operator dropdown is as wide as its widest wording (OperatorLabel), so a second
		// value ticked ("is" to "is any of") never widens it.
		float OperatorWidth(SituationRules::Kind a_kind)
		{
			using SituationRules::Kind;
			if (a_kind == Kind::kTimeOfDay) {
				return WidestDropdown({ "between", "not between" });
			}
			if (a_kind == Kind::kSpell) {
				return WidestDropdown({ "has", "has not", "has any of", "has none of" });
			}
			if ((SituationRules::HasValues(a_kind) && !SituationRules::OneValue(a_kind)) || SituationRules::HasForms(a_kind)) {
				return WidestDropdown({ "is", "is not", "is any of", "is none of" });
			}
			return WidestDropdown({ "is", "is not" });
		}

		// A dropdown of two choices, `a_positive` while `a_negated` is off and `a_negative` on: a
		// row's operator, `a_width` wide (OperatorWidth), or a two-state condition itself, as wide
		// as its wider choice with `a_width` 0. Returns true when switched; `a_hovered` tells the
		// caller to show the kind's help (never while open).
		bool DrawNegationDropdown(const char* a_id, const char* a_positive, const char* a_negative, float a_width, bool& a_negated, bool& a_hovered)
		{
			const Theme::FieldStyle style{ .filled = true };
			ImGui::SetNextItemWidth(a_width > 0.0f ? a_width : WidestDropdown({ a_positive, a_negative }));
			const bool open = Theme::BeginCombo(a_id, ImGuiComboFlags_None, style);
			a_hovered = !open && ImGui::IsItemHovered();
			bool changed = false;
			if (open) {
				for (const bool negative : { false, true }) {
					bool chosen = negative == a_negated;
					if (Theme::DropdownRow(negative ? a_negative : a_positive, &chosen, false) && negative != a_negated) {
						a_negated = negative;
						changed = true;
					}
				}
			}
			Theme::EndCombo(open, a_negated ? a_negative : a_positive, style);
			return changed;
		}

		// Whose state the row reads (SituationRules::Target): the wearer, named as the actor, or
		// the player. In the player's own window both name them, so the help tells the wearer
		// (whoever wears the outfit) from the player (always you). Returns true when switched.
		bool DrawTargetDropdown(const CaptureWindow::Request& a_request, SituationRules::Row& a_row)
		{
			constexpr const char* kPlayer = "Player";

			const char*             wearer = a_request.actorName.c_str();
			const Theme::FieldStyle style{ .filled = true };
			ImGui::SetNextItemWidth(WidestDropdown({ wearer, kPlayer }));
			const bool open = Theme::BeginCombo("##target", ImGuiComboFlags_None, style);
			const bool hovered = !open && ImGui::IsItemHovered();
			bool       changed = false;
			if (open) {
				for (const auto target : { SituationRules::Target::kWearer, SituationRules::Target::kPlayer }) {
					bool chosen = target == a_row.target;
					if (Theme::DropdownRow(target == SituationRules::Target::kPlayer ? kPlayer : wearer, &chosen, false) && target != a_row.target) {
						a_row.target = target;
						changed = true;
					}
				}
			}
			Theme::EndCombo(open, a_row.target == SituationRules::Target::kPlayer ? kPlayer : wearer, style);
			if (hovered) {
				Theme::Tooltip((a_request.actorIsPlayer ? std::format("{}: checks each character wearing the outfit\nPlayer: always checks you", wearer) :
														  std::format("{}: checks this character\nPlayer: checks you", wearer))
						.c_str());
			}
			return changed;
		}

		// The values of `a_row`'s kind as a multi-choice dropdown, the ticked ones its preview
		// ("Rain, Snow"), a greyed Select... with none; a single choice, closed by it, for a kind
		// whose rows take one (SituationRules::OneValue); a rule between groups of values
		// (Value::ruleAbove). `a_width` is the room it takes. Returns true when one was ticked,
		// unticked or chosen.
		bool DrawValuesDropdown(SituationRules::Row& a_row, float a_width)
		{
			constexpr const char* kSelect = "Select...";

			const bool  one = SituationRules::OneValue(a_row.kind);
			const auto  values = SituationRules::InfoOf(a_row.kind).values;
			std::string preview;
			for (const auto& value : values) {
				if (a_row.values & value.bit) {
					preview += (preview.empty() ? "" : ", ") + std::string(value.label);
				}
			}
			const Theme::FieldStyle style{ .previewIsHint = preview.empty(), .filled = true };
			ImGui::SetNextItemWidth(a_width);
			const bool open = Theme::BeginCombo("##values", ImGuiComboFlags_HeightLargest, style);
			bool       changed = false;
			if (open) {
				for (const auto& value : values) {
					if (value.ruleAbove) {
						ImGui::Separator();
					}
					bool ticked = (a_row.values & value.bit) != 0;
					if (Theme::DropdownRow(value.label, &ticked, !one)) {  // ticks keep it open for more
						changed = one ? a_row.values != value.bit : true;
						a_row.values = one ? value.bit : a_row.values ^ value.bit;
					}
					if (*value.tip && ImGui::IsItemHovered()) {
						Theme::Tooltip(value.tip);
					}
				}
			}
			Theme::EndCombo(open, preview.empty() ? kSelect : preview.c_str(), style);
			return changed;
		}

		// The widths a values dropdown asks for: the least that shows its widest value, and the
		// most it needs, every value ticked (the least for a single choice).
		struct ValuesWidth
		{
			float least;
			float most;
		};

		ValuesWidth ValuesDropdownWidth(SituationRules::Kind a_kind)
		{
			const Theme::FieldStyle style{ .filled = true };
			std::string             all;
			float                   least = 0.0f;
			for (const auto& value : SituationRules::InfoOf(a_kind).values) {
				least = (std::max)(least, Theme::DropdownWidth(value.label, style));
				all += (all.empty() ? "" : ", ") + std::string(value.label);
			}
			return { least, SituationRules::OneValue(a_kind) ? least : Theme::ComboWidth(all.c_str(), style) };
		}

		// A dropdown of the times of day, `a_minutes` (after midnight) its choice. Returns true
		// when another was chosen.
		bool DrawTimeDropdown(const char* a_id, std::uint16_t& a_minutes)
		{
			constexpr std::uint16_t kTimeStep = 30;  // minutes between the times offered

			const Theme::FieldStyle style{ .filled = true };
			const auto              current = SituationRules::TimeText(a_minutes);
			ImGui::SetNextItemWidth(Theme::DropdownWidth(kWidestTime, style));
			const bool open = Theme::BeginCombo(a_id, ImGuiComboFlags_HeightLarge, style);
			bool       changed = false;
			if (open) {
				for (std::uint16_t minute = 0; minute < SituationRules::kMinutesPerDay; minute += kTimeStep) {
					bool chosen = minute == a_minutes;
					if (Theme::DropdownRow(SituationRules::TimeText(minute).c_str(), &chosen, false) && minute != a_minutes) {
						a_minutes = minute;
						changed = true;
					}
				}
			}
			Theme::EndCombo(open, current.c_str(), style);
			return changed;
		}

		// AlignTextToFramePadding for a condition's line: its plain text level with its
		// dropdowns' text, which sits by the field's own padding (Theme::FieldHeight), not the
		// skin's frame padding (Vanilla's and Nordic's underlined fields).
		void AlignTextToField()
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, (Theme::FieldHeight({}) - ImGui::GetFontSize()) * 0.5f));
			ImGui::AlignTextToFramePadding();
			ImGui::PopStyleVar();
		}

		// `[8 PM] and [6 AM]`, a Time of day row's range. Returns true when a time was changed.
		bool DrawTimeRange(SituationRules::Row& a_row)
		{
			bool changed = DrawTimeDropdown("##from", a_row.from);
			ImGui::SameLine();
			AlignTextToField();
			ImGui::TextUnformatted("and");
			ImGui::SameLine();
			return DrawTimeDropdown("##to", a_row.to) || changed;
		}

		float TimeRangeWidth()
		{
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			return Theme::DropdownWidth(kWidestTime, Theme::FieldStyle{ .filled = true }) * 2.0f + ImGui::CalcTextSize("and").x + spacing * 2.0f;
		}

		// Whether `a_width` more fits on the line just laid out, before `a_right`.
		bool FitsOnLine(float a_width, float a_right)
		{
			return ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + a_width <= a_right;
		}

		// A × (Theme::ClearButton) `a_side` square on the line just laid out, its centre on
		// `a_centreX` and its top at `a_top`; `a_tip` on hover. Returns true when clicked. The
		// next item starts under the line, so lay it out before anything that wraps under it.
		bool DrawRemoveCross(const char* a_id, float a_centreX, float a_top, float a_side, const char* a_tip)
		{
			ImGui::SameLine();
			ImGui::SetCursorScreenPos(ImVec2(a_centreX - a_side * 0.5f, a_top));
			const bool clicked = Theme::ClearButton(a_id, a_side);
			if (ImGui::IsItemHovered()) {
				Theme::Tooltip(a_tip);
			}
			return clicked;
		}

		enum class ConditionEdit
		{
			kNone,
			kChanged,
			kRemove,
			kAddForm  // a Place row's Add Location... or a Spell row's Add Spell... pressed
		};

		// A spell's hover: its plugin and FormID, then its visible effects a line each
		// (SituationRules::SpellChoice::effectLines; none when `a_effectLines` is empty).
		std::string SpellTip(const SituationRules::Spell& a_spell, const std::string& a_effectLines)
		{
			const auto head = std::format("{} {:06X}", a_spell.ref.plugin, a_spell.ref.localID);
			return a_effectLines.empty() ? head : head + "\n" + a_effectLines;
		}

		// The effect lines the Add spell popup's lists have for a spell of a row: the same spell,
		// or one it was merged with (the same name and effects); "" when neither list has it.
		const std::string& EffectLinesOf(const CaptureWindow::Request& a_request, const SituationRules::Spell& a_spell)
		{
			static const std::string none;
			const auto               same = [&a_spell](const SituationRules::SpellChoice& a_choice) {
				const auto& spell = a_choice.spell;
				return SituationRules::SameForm(spell.ref, a_spell.ref) ||
				       (!a_spell.effects.empty() && spell.effects == a_spell.effects && ExampleMod::CaseInsensitiveEqual(spell.ref.name, a_spell.ref.name));
			};
			using Choices = std::span<const SituationRules::SpellChoice>;
			for (const Choices list : { Choices(a_request.spellsWithPlayer), Choices(a_request.spells), a_request.allSpells }) {
				if (const auto found = std::ranges::find_if(list, same); found != list.end()) {
					return found->effectLines;
				}
			}
			return none;
		}

		// A Place or Spell row's forms, a line each with a × centred on `a_crossX`, one whose
		// plugin is not loaded greyed and saying so on hover, a spell's effects on hover; then Add
		// Location... or Add Spell.... Returns true when one was removed; sets `a_add` when the Add
		// link was pressed.
		bool DrawFormList(const CaptureWindow::Request& a_request, SituationRules::Row& a_row, float a_crossX, bool& a_add)
		{
			const bool                 place = a_row.kind == SituationRules::Kind::kPlace;
			auto&                      forms = SituationRules::FormsOf(a_row);
			std::optional<std::size_t> removed;
			for (std::size_t i = 0; i < forms.size(); ++i) {
				const auto& form = forms[i];
				const bool  loaded = form.formID != 0;
				ImGui::PushID(static_cast<int>(i));
				if (!loaded) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				}
				ImGui::TextUnformatted(SituationRules::FormName(form).c_str());
				if (!loaded) {
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) {
						Theme::Tooltip(std::format("{} is not loaded.", form.ref.plugin).c_str());
					}
				} else if (!place && ImGui::IsItemHovered()) {
					Theme::Tooltip(SpellTip(form, EffectLinesOf(a_request, form)).c_str());
				}
				if (DrawRemoveCross("##remove", a_crossX, ImGui::GetItemRectMin().y, ImGui::GetItemRectSize().y, place ? "Remove this location." : "Remove this spell.")) {
					removed = i;
				}
				ImGui::PopID();
			}
			if (removed) {
				forms.erase(forms.begin() + static_cast<std::ptrdiff_t>(*removed));
			}
			a_add = Theme::LabelLink(place ? "Add Location..." : "Add Spell...", Theme::LinkLine::kNextLeft);
			return removed.has_value();
		}

		// A row's first words for a kind that is not targetable (KindInfo::subject, the actor
		// named); "" for a targetable kind, whose row starts with whose state.
		std::string SubjectOf(const CaptureWindow::Request& a_request, const SituationRules::KindInfo& a_info)
		{
			return std::vformat(a_info.subject, std::make_format_args(a_request.actorName));
		}

		// How wide a row of `a_kind` is with every value shown on it: whose state or its subject,
		// the operator or state dropdown, then the phrase, the values or the times. A Place or
		// Spell row lists its forms under it, so only its first line counts.
		float ConditionRowWidth(const CaptureWindow::Request& a_request, SituationRules::Kind a_kind)
		{
			const auto& info = SituationRules::InfoOf(a_kind);
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			float       width = info.targetable ? WidestDropdown({ a_request.actorName.c_str(), "Player" }) : ImGui::CalcTextSize(SubjectOf(a_request, info).c_str()).x;
			if (SituationRules::TwoState(a_kind) && *info.state) {
				width += gap + WidestDropdown({ info.state, info.negState });
			} else {
				width += gap + OperatorWidth(a_kind);
				if (SituationRules::TwoState(a_kind)) {
					width += gap + ImGui::CalcTextSize(info.phrase).x;
				}
			}
			if (!info.values.empty()) {
				width += gap + ValuesDropdownWidth(a_kind).most;
			} else if (a_kind == SituationRules::Kind::kTimeOfDay) {
				width += gap + TimeRangeWidth();
			}
			return width;
		}

		// One condition: its heading (the kind's name, its help on the (?), a Remove link at the
		// line's right end, `a_lineRight`), then the row under it as the section says; a Place or
		// Spell row lists its forms under that, each with a ×; values or times that do not fit the
		// row go under it.
		ConditionEdit DrawConditionRow(const CaptureWindow::Request& a_request, SituationRules::Row& a_row, float a_lineRight)
		{
			constexpr float kListIndent = 16.0f;  // pixels at 1080p, what sits under the row in from its left

			const auto& info = SituationRules::InfoOf(a_row.kind);
			const float side = ImGui::GetFrameHeight();
			const float crossX = a_lineRight - side * 0.5f;
			Theme::LabelText(info.label, Theme::HeadingIcon::kNone, false);  // the box sets the conditions apart
			if (Theme::LabelHelpMark()) {
				Theme::Tooltip(std::vformat(info.tip, std::make_format_args(a_request.actorName)).c_str());
			}
			const bool removed = Theme::LabelLineLink("Remove");  // a word, not a ×, apart from the forms' ×s under it

			const bool time = a_row.kind == SituationRules::Kind::kTimeOfDay;
			bool       changed = false;
			bool       ignored = false;
			if (info.targetable) {
				changed = DrawTargetDropdown(a_request, a_row);
				ImGui::SameLine();
			} else {
				AlignTextToField();
				ImGui::TextUnformatted(SubjectOf(a_request, info).c_str());
				ImGui::SameLine();
			}
			if (SituationRules::TwoState(a_row.kind) && *info.state) {
				changed = DrawNegationDropdown("##state", info.state, info.negState, 0.0f, a_row.negated, ignored) || changed;
			} else {
				changed = DrawNegationDropdown("##operator", time ? "between" : OperatorLabel(a_row, false), time ? "not between" : OperatorLabel(a_row, true),
							  OperatorWidth(a_row.kind), a_row.negated, ignored) ||
				          changed;
				if (SituationRules::TwoState(a_row.kind)) {
					ImGui::SameLine();
					AlignTextToField();
					ImGui::TextUnformatted(info.phrase);
				}
			}
			// The values on the row while they fit, else under it.
			bool under = false;
			if (!info.values.empty()) {
				const auto width = ValuesDropdownWidth(a_row.kind);
				if (FitsOnLine(width.least, a_lineRight)) {
					ImGui::SameLine();
					changed = DrawValuesDropdown(a_row, (std::min)(width.most, a_lineRight - ImGui::GetCursorScreenPos().x)) || changed;
				} else {
					under = true;
				}
			} else if (time) {
				if (FitsOnLine(TimeRangeWidth(), a_lineRight)) {
					ImGui::SameLine();
					changed = DrawTimeRange(a_row) || changed;
				} else {
					under = true;
				}
			}
			bool addForm = false;
			if (under || SituationRules::HasForms(a_row.kind)) {
				const float indent = kListIndent * Theme::Scale();
				ImGui::Indent(indent);
				if (!info.values.empty()) {
					changed = DrawValuesDropdown(a_row, (std::min)(ValuesDropdownWidth(a_row.kind).most, a_lineRight - ImGui::GetCursorScreenPos().x)) || changed;
				} else if (time) {
					changed = DrawTimeRange(a_row) || changed;
				} else {
					changed = DrawFormList(a_request, a_row, crossX, addForm) || changed;
				}
				ImGui::Unindent(indent);
			}
			if (removed) {
				return ConditionEdit::kRemove;
			}
			if (addForm) {
				return ConditionEdit::kAddForm;
			}
			return changed ? ConditionEdit::kChanged : ConditionEdit::kNone;
		}

		// A two-state kind the situation has every row it can take of: one, or with a target to
		// choose, one per target. A second adds nothing.
		bool KindExhausted(const SituationRules::Rules& a_rules, SituationRules::Kind a_kind)
		{
			if (!SituationRules::TwoState(a_kind)) {
				return false;
			}
			bool wearer = false;
			bool player = false;
			for (const auto& row : a_rules.rows) {
				if (row.kind == a_kind) {
					(row.target == SituationRules::Target::kPlayer ? player : wearer) = true;
				}
			}
			return (wearer || player) && (!SituationRules::Targetable(a_kind) || (wearer && player));
		}

		// The kinds by label, ASCII case ignored, as Add Condition... lists them.
		const std::array<const SituationRules::KindInfo*, SituationRules::kKinds.size()>& KindsByLabel()
		{
			static const auto sorted = [] {
				std::array<const SituationRules::KindInfo*, SituationRules::kKinds.size()> kinds{};
				for (std::size_t i = 0; i < kinds.size(); ++i) {
					kinds[i] = &SituationRules::kKinds[i];
				}
				const auto lower = [](char a_char) { return a_char >= 'A' && a_char <= 'Z' ? static_cast<char>(a_char - 'A' + 'a') : a_char; };
				std::ranges::sort(kinds, [&lower](const SituationRules::KindInfo* a_left, const SituationRules::KindInfo* a_right) {
					return std::ranges::lexicographical_compare(std::string_view(a_left->label), std::string_view(a_right->label), {}, lower, lower);
				});
				return kinds;
			}();
			return sorted;
		}

		// Add Condition...: a dropdown of the kinds by label, closed by a choice; returns the one
		// chosen. A two-state kind the situation has every row it can take of is greyed
		// (KindExhausted).
		std::optional<SituationRules::Kind> DrawAddCondition(const SituationRules::Rules& a_rules)
		{
			constexpr const char* kAddCondition = "Add Condition...";

			const Theme::FieldStyle style{ .icon = Theme::FieldIcon::kAdd, .previewIsHint = true };
			ImGui::SetNextItemWidth(Theme::DropdownWidth(kAddCondition, style));
			const bool                          open = Theme::BeginCombo("##addCondition", ImGuiComboFlags_HeightLargest, style);
			std::optional<SituationRules::Kind> added;
			if (open) {
				for (const auto* info : KindsByLabel()) {
					bool chosen = false;
					ImGui::BeginDisabled(KindExhausted(a_rules, info->kind));
					if (Theme::DropdownRow(info->label, &chosen, false)) {
						added = info->kind;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndDisabled();
				}
			}
			Theme::EndCombo(open, kAddCondition, style);
			return added;
		}

		// Allow during combat, ticked and greyed while an In combat condition needs it.
		bool DrawAllowDuringCombat(const State& a_state, SituationRules::Rules& a_rules)
		{
			const bool forced = !a_rules.applyInCombat && SituationRules::AppliesInCombat(a_rules);
			bool       ticked = forced || a_rules.applyInCombat;
			BeginItemDisabled(a_state, forced);
			const bool changed = Theme::Checkbox("Allow during combat", &ticked);
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			EndItemDisabled();
			// After EndDisabled, so the tooltip is not drawn faded.
			if (hovered) {
				Theme::Tooltip(forced ? "On while the situation has an In combat condition." :
										"In combat, the default outfit is normally worn.\nOn, this situation can apply in combat too.");
			}
			if (changed) {
				a_rules.applyInCombat = ticked;
			}
			return changed;
		}

		// The fill inside the box of the condition clicked in the Applies text
		// (SituationForm::editorFocus), its top left `a_topLeft`, as tall as the box was last
		// drawn, fading out; scrolls the editor to it while SituationForm::editorScroll frames
		// remain.
		void LightFocusedCondition(SituationForm& a_form, ImVec2 a_topLeft, float a_right)
		{
			constexpr double kLightSeconds = 1.5;  // from the editor's opening to the fill gone

			if (a_form.editorScroll > 0) {
				ImGui::SetScrollHereY(0.0f);
				--a_form.editorScroll;
			}
			const double lit = 1.0 - (ImGui::GetTime() - a_form.editorFocusFrom) / kLightSeconds;
			if (lit <= 0.0 || a_form.editorFocusHeight <= 0.0f) {
				return;
			}
			ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_Header);  // a selected row's
			fill.w *= static_cast<float>(lit);
			ImGui::GetWindowDrawList()->AddRectFilled(a_topLeft, ImVec2(a_right, a_topLeft.y + a_form.editorFocusHeight), ImGui::GetColorU32(fill),
				kConditionBoxRounding * Theme::Scale());
		}

		// The editor's content width: the widest row any kind can have (ConditionRowWidth) in its
		// box, so only a Place or Spell row's list goes under its row. The scroll bar goes past it
		// (Theme::BeginScrollArea).
		float EditorContentWidth(const CaptureWindow::Request& a_request)
		{
			float widest = 0.0f;
			for (const auto& info : SituationRules::kKinds) {
				widest = (std::max)(widest, ConditionRowWidth(a_request, info.kind));
			}
			return widest + std::round(kConditionBoxPadding.x * Theme::Scale()) * 2.0f;
		}

		// A user's situation's conditions as edited: a row each (DrawConditionRow) in a box of a
		// 1px rule-coloured line with round corners, the one clicked in the Applies text lit
		// (LightFocusedCondition), then Add Condition... and Allow during combat. Returns true on
		// a change; sets `a_addForm` when a Place or Spell row's Add link was pressed, that row in
		// SituationForm::formRow.
		bool DrawConditionEditor(State& a_state, SituationRules::Rules& a_rules, bool& a_addForm)
		{
			auto&                      form = a_state.situationForm;
			const float                scale = Theme::Scale();
			const ImVec2               padding(std::round(kConditionBoxPadding.x * scale), std::round(kConditionBoxPadding.y * scale));
			const float                boxRight = std::round(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);
			auto*                      draw = ImGui::GetWindowDrawList();
			bool                       changed = false;
			std::optional<std::size_t> removed;
			for (std::size_t i = 0; i < a_rules.rows.size(); ++i) {
				const bool   focused = static_cast<int>(i) == form.editorFocus;
				const ImVec2 cursor = ImGui::GetCursorScreenPos();
				const ImVec2 topLeft(std::round(cursor.x), std::round(cursor.y));
				if (focused) {
					LightFocusedCondition(form, topLeft, boxRight);
				}
				ImGui::SetCursorScreenPos(ImVec2(topLeft.x, topLeft.y + padding.y));
				ImGui::Indent(padding.x);
				// The padding on the right too, for what aligns to the content's right edge (the
				// heading's Remove, Theme::LabelLineLink). Contract: inside the scroll area, a child window
				// with no table, whose content region GetContentRegionAvail reads.
				auto* window = ImGui::GetCurrentWindow();
				window->ContentRegionRect.Max.x -= padding.x;
				window->WorkRect.Max.x -= padding.x;
				ImGui::PushID(static_cast<int>(i));
				const auto edit = DrawConditionRow(a_state.request, a_rules.rows[i], boxRight - padding.x);
				ImGui::PopID();
				window->WorkRect.Max.x += padding.x;
				window->ContentRegionRect.Max.x += padding.x;
				ImGui::Unindent(padding.x);
				const float bottom = std::round(ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + padding.y);
				draw->AddRect(topLeft, ImVec2(boxRight, bottom), ImGui::GetColorU32(ImGuiCol_Border), kConditionBoxRounding * scale, ImDrawFlags_None, 1.0f);
				if (focused) {
					form.editorFocusHeight = bottom - topLeft.y;
				}
				ImGui::SetCursorScreenPos(ImVec2(topLeft.x, bottom + std::round(kConditionBoxGap * scale)));
				if (edit == ConditionEdit::kAddForm) {
					a_state.situationForm.formRow = static_cast<int>(i);
					a_addForm = true;
					continue;
				}
				changed = changed || edit != ConditionEdit::kNone;
				if (edit == ConditionEdit::kRemove) {
					removed = i;
				}
			}
			if (removed) {
				a_rules.rows.erase(a_rules.rows.begin() + static_cast<std::ptrdiff_t>(*removed));
				form.editorFocus = -1;  // its index may now name another
			}
			if (const auto kind = DrawAddCondition(a_rules)) {
				// A kind whose rows take one value starts with its first, never with none.
				a_rules.rows.push_back({ .kind = *kind, .values = SituationRules::OneValue(*kind) ? SituationRules::InfoOf(*kind).values.front().bit : 0u });
				changed = true;
				// A Place or Spell row starts empty, so its Add link is pressed for the user.
				if (SituationRules::HasForms(*kind)) {
					form.formRow = static_cast<int>(a_rules.rows.size() - 1);
					a_addForm = true;
				}
			}
			Theme::SectionGap();  // not a condition: apart from the list
			changed = DrawAllowDuringCombat(a_state, a_rules) || changed;
			if (changed) {
				QueueMenuSound(kFocusSound);
			}
			return changed;
		}

		// --- Right pane: the outfit list --------------------------------------

		// The right pane shows the details of the outfit clicked last (`a_viewed`, its index into
		// request.existingOutfits, -1 for none) until Back; else, and while a drag from the
		// Situation List is on, the outfit list.
		bool ShowsOutfitView(int a_viewed)
		{
			const auto* dragged = DraggedOutfit();
			return a_viewed >= 0 && !(dragged && dragged->from >= 0);
		}

		// The Outfit List (?) label over the outfit list with its own search and sort, its rows
		// dragged as copies and a click on one showing its details (`a_viewed`'s row selected). A
		// drag from the Situation List dropped anywhere on it takes that outfit, or the Default's
		// pieces, off its row. Returns whether Escape cleared the search.
		bool DrawOutfitPane(State& a_state, int a_viewed)
		{
			constexpr float kFrameWidth = 2.0f;  // pixels at 1080p, around the pane while a drop would take an outfit off

			const ImVec2 top = ImGui::GetCursorScreenPos();
			const float  width = ImGui::GetContentRegionAvail().x;
			Theme::LabelText("Outfit List");
			const bool helpHovered = Theme::LabelHelpMark();
			if (FocusStandIn("##outfitListHelp") || helpHovered) {
				DrawHelpTooltip(kOutfitListHelp);
			}
			const auto  controls = DrawBrowserControls(a_state, a_state.situationBrowser, true);
			const int   clicked = DrawOutfitList(a_state, a_state.situationBrowser, a_viewed, true);
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
							DropOnOutfitList(a_state, *static_cast<const OutfitPayload*>(payload->Data));
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (clicked >= 0) {
				ViewOutfit(a_state, clicked, -1);  // DrawOutfitList played the focus sound
			}
			return controls.escapeTaken;
		}

		// --- Edit situation: settings -----------------------------------------

		// What the Default is, under its name.
		void DrawDefaultNote(const State& a_state)
		{
			const auto whose = std::format("This is {}'s default outfit.", a_state.request.actorName);
			Theme::SectionGap();
			RunLine({ TextRun(whose.c_str()) });
		}

		// The Default's conditions as Situations::ConditionsText words them, with the star of the
		// situations before "situation", all in the text colour pushed now, where a field's text
		// sits (DrawFieldText); its two lines as close as a wrapped text's.
		void DrawDefaultConditions()
		{
			const std::array<Theme::SubtitleRun, 3> first{ { TextRun("Worn where no "), SituationRun(""), TextRun("situation applies,") } };
			const std::array<Theme::SubtitleRun, 1> second{ { TextRun("and in combat.") } };
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
			Theme::RunText(first, true);
			ImGui::PopStyleVar();
			Theme::RunText(second, true);
		}

		// Why a situation is never worn (DrawNeverWornText), in the error colour, wrapped to the pane.
		void DrawNeverWorn(const SituationForm& a_form, std::size_t a_entry)
		{
			const auto by = NeverWornBy(a_form, a_entry);
			if (!by) {
				return;
			}
			Theme::SectionGap();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ErrorColor());
			DrawNeverWornText(a_form, *by);
			ImGui::PopStyleColor();
		}

		// "Always overrides Rainy town." for each situation higher in the list that entry
		// `a_entry` applies whenever it does, so that one is never worn (Overrides): in the error
		// colour, one line each in the list's order, as close as a wrapped text's lines.
		void DrawOverrides(const SituationForm& a_form, std::size_t a_entry)
		{
			std::vector<std::size_t> overridden;
			for (const auto other : a_form.order) {
				if (Overrides(a_form, static_cast<int>(other), a_entry)) {
					overridden.push_back(other);
				}
			}
			if (overridden.empty()) {
				return;
			}
			Theme::SectionGap();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ErrorColor());
			for (std::size_t i = 0; i < overridden.size(); ++i) {
				const bool                              last = i + 1 == overridden.size();
				const auto                              name = Trimmed(a_form.entries[overridden[i]].name.data());
				const std::array<Theme::SubtitleRun, 3> line{ { TextRun("Always overrides "), SituationRun(name.c_str()), TextRun(".") } };
				if (!last) {
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
				}
				Theme::RunTextWrapped(line);
				if (!last) {
					ImGui::PopStyleVar();
				}
			}
			ImGui::PopStyleColor();
		}

		// The number of the last ask handed to Request::onCheckConditions, never reset, so an
		// answer to an ask of a window closed since matches none.
		// Contract: under the window state's mutex (the page draws, SetConditionsHeld reads the form).
		std::uint32_t g_checkAsks = 0;

		// Asks Request::onCheckConditions whether each of `a_rules`' conditions is met now: at once
		// when they differ from those last asked (another situation, an edit), else every
		// kHoldsRefresh once the last ask was answered, so a stalled game thread gets no queue of
		// asks. SetConditionsHeld takes the answer to the latest ask only.
		void CheckConditions(State& a_state, const SituationRules::Rules& a_rules)
		{
			auto&       form = a_state.situationForm;
			const auto& onCheck = a_state.request.onCheckConditions;
			if (!onCheck || a_rules.rows.empty()) {
				return;
			}
			const double now = ImGui::GetTime();
			if (a_rules.rows == form.checkedRows && (!form.checkAnswered || now - form.checkedAt < kHoldsRefresh)) {
				return;
			}
			form.checkedRows = a_rules.rows;
			form.checkedAt = now;
			form.checkAnswered = false;
			form.checkAsk = ++g_checkAsks;
			SKSE::GetTaskInterface()->AddTask([onCheck, ask = form.checkAsk, rows = a_rules.rows] { onCheck(ask, rows); });
		}

		// A user's situation's Applies text as SituationRules::Text words it, each condition a link
		// (Theme::RunLinks), where a field's text sits (DrawFieldText), in the text colour pushed
		// now, a condition met now (CheckConditions) in the palette's positive colour: lit while
		// hovered with the editor on offer (Request::customSituations); its tooltip says whether it
		// is met now, and without the editor how to offer it. Returns the condition clicked (index
		// into rules.rows), or -1.
		int DrawAppliesLinks(const State& a_state, const SituationRules::Rules& a_rules)
		{
			if (a_rules.rows.empty()) {
				DrawFieldText(SituationRules::Text(a_rules).c_str());  // "Never applies."
				return -1;
			}
			std::vector<std::string> phrases;
			phrases.reserve(a_rules.rows.size());
			for (const auto& row : a_rules.rows) {
				phrases.push_back(SituationRules::RowText(row));
			}
			auto& first = phrases.front();
			if (!first.empty() && first[0] >= 'a' && first[0] <= 'z') {
				first[0] = static_cast<char>(first[0] - 'a' + 'A');
			}
			// The answer to the latest ask, while it is one per row: an edit's rows are coloured by the
			// answer before it until its own comes, a frame or so later.
			const auto& held = a_state.situationForm.conditionsHeld;
			const bool  known = held.size() == a_rules.rows.size();
			const auto  positive = Theme::PositiveColor();
			// A phrase per even index, the commas and the full stop between: run / 2 is the row.
			std::vector<Theme::LinkRun> runs;
			runs.reserve(phrases.size() * 2);
			for (std::size_t i = 0; i < phrases.size(); ++i) {
				if (i > 0) {
					runs.push_back({ ", ", false });
				}
				runs.push_back({ phrases[i].c_str(), true, known && held[i] ? positive : 0u });
			}
			runs.push_back({ ".", false });

			const bool editable = a_state.request.customSituations;
			int        hovered = -1;
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
			const int clicked = Theme::RunLinks(runs, editable, &hovered);
			if (hovered >= 0) {
				std::string tip = known ? (held[static_cast<std::size_t>(hovered / 2)] ? kMetNow : kNotMetNow) : "";
				if (!editable) {
					tip += tip.empty() ? kTurnOnCustom : std::format("\n\n{}", kTurnOnCustom);
				}
				if (!tip.empty()) {
					Theme::Tooltip(tip.c_str());
				}
			}
			return editable && clicked >= 0 ? clicked / 2 : -1;
		}

		// The Edit situation popup's settings, its left column: the name (the Default says what it
		// is under it), why it is never
		// worn, the situations it always overrides, `Applies` over the conditions, the Default's
		// read only, another's Applies text (DrawAppliesLinks) with the label's help, greyed and
		// read only unless the page offers the editor (Request::customSituations); then Random
		// Outfit (for #{Name}). Returns the condition clicked in the Applies text (index into
		// rules.rows), or -1.
		int DrawSituationSettings(State& a_state, std::size_t a_entry)
		{
			auto&       form = a_state.situationForm;
			auto&       entry = form.entries[a_entry];
			const auto& name = a_state.request.actorName;
			const bool  isDefault = a_entry == 0;
			Theme::LabelText("Name");
			ImGui::SetNextItemWidth(-FLT_MIN);
			Theme::InputText("##name", entry.name.data(), entry.name.size(), ImGuiInputTextFlags_None, kNeedName);
			if (ImGui::IsItemActivated()) {
				entry.nameBefore = entry.name.data();
				form.nameEditing = static_cast<int>(a_entry);
				form.nameField = ImGui::GetItemID();
			}
			if (isDefault) {
				DrawDefaultNote(a_state);
			}
			DrawNeverWorn(form, a_entry);
			DrawOverrides(form, a_entry);
			Theme::LabelText("Applies");
			int        clicked = -1;
			const bool editable = !isDefault && a_state.request.customSituations;
			if (!isDefault && Theme::LabelHelpMark()) {
				DrawHelpTooltip(kConditionsHelp);
			}
			if (!editable) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}
			if (isDefault) {
				DrawDefaultConditions();
			} else {
				clicked = DrawAppliesLinks(a_state, entry.rules);
			}
			if (!editable) {
				ImGui::PopStyleColor();
			}

			Theme::SectionGap();  // a field of its own, with no label
			bool              random = entry.row.random;
			const std::string randomLabel = std::format("Random Outfit (for {})", name);
			if (Theme::Checkbox(randomLabel.c_str(), &random)) {
				QueueMenuSound(kFocusSound);
				OutfitRows::SetRandom(entry.row, random);  // off keeps the last pick on
				ScheduleAutosave(a_state);
				// An outfit the Default wears with random off drops its pieces, as a drop does.
				if (isDefault && !random && entry.row.chosen != 0) {
					form.defaultPieces = false;
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			const bool helpHovered = ImGui::IsItemHovered();
			if (FocusStandIn("##randomHelp") || helpHovered) {
				DrawHelpTooltip(std::format(
					"On: Each time this situation begins, {0} wears a random outfit from the list until it ends.\n\n"
					"Off: {0} wears the last picked outfit. The others stay in the list but are hidden.",
					name));
			}
			return clicked;
		}

		// --- Right pane: an outfit's details ----------------------------------

		enum class OutfitViewAction
		{
			kNone,
			kOpenWardrobe,
			kBack
		};

		// The rows the page's edits give outfit `a_outfit`, named as the Wardrobe page's dropdown
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

		// The outfit clicked last (`a_viewed`, its index into request.existingOutfits) as Manage
		// Wardrobe lays it out (DrawOutfitSummary), its rows as the page's edits have them, its
		// pieces ticked as the row it was clicked under wears them, a tick an edit of that row
		// (read only from the outfit list, or once the row lost it); then Open in Wardrobe and Back
		// centred, one over the other when the pane leaves them no item spacing on either side,
		// as far apart as buttons side by side are.
		// It grows with the pieces, moving the page's buttons when taller than the Situation
		// List; its pieces' list scrolls only to keep the window within kMaxWindowHeightRatio of
		// the screen, as every list does (ListBottom). Escape is Back, and sets `a_escapeTaken` so it is not also Exit.
		// Contract: ShowsOutfitView is true; called at the top of the right pane.
		OutfitViewAction DrawOutfitView(State& a_state, int a_viewed, bool& a_escapeTaken)
		{
			const float     spacing = ImGui::GetStyle().ItemSpacing.x;
			const RowButton open{ "Open in Wardrobe", Theme::HeadingIcon::kWardrobe };
			const bool      stacked = ButtonRowWidth({ open, "Back" }) + spacing * 2.0f > ImGui::GetContentRegionAvail().x;
			const float     buttonRow = ImGui::GetStyle().ItemSpacing.y + ButtonRowGap() + Theme::ButtonHeight() +
			                            (stacked ? spacing + Theme::ButtonHeight() : 0.0f);  // under the pieces' list
			const float     bottom = ListBottom();                                           // the view's lowest
			ImGui::PushID("outfitView");
			auto&      form = a_state.situationForm;
			const auto edited = EditedRow(form);
			auto*      row = edited >= 0 ? &form.entries[static_cast<std::size_t>(edited)].row : nullptr;
			auto       excluded = row ? OutfitRows::ExcludedOf(*row, form.viewedOutfit) : std::vector<std::uint32_t>{};
			if (DrawOutfitSummary(a_state, a_viewed, HeldRowsText(a_state, form.viewedOutfit), bottom - buttonRow, row ? &excluded : nullptr)) {
				OutfitRows::SetExcluded(*row, form.viewedOutfit, std::move(excluded));
				QueuePreview(a_state, ViewedOutfitPieces(a_state));
				ScheduleAutosave(a_state);
			}
			if (stacked) {
				BeginButtonRow({ open });
			} else {
				BeginButtonRow({ open, "Back" });
			}
			const bool openWardrobe = Theme::Button(open.icon, open.label);
			if (stacked) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + spacing - ImGui::GetStyle().ItemSpacing.y);
				CentreButtonRow({ "Back" });
			} else {
				ImGui::SameLine();
			}
			const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			const bool back = Theme::Button("Back") || escape;
			ImGui::PopID();
			a_escapeTaken = a_escapeTaken || escape;
			if (openWardrobe) {
				return OutfitViewAction::kOpenWardrobe;
			}
			return back ? OutfitViewAction::kBack : OutfitViewAction::kNone;
		}

		// --- Buttons, popups --------------------------------------------------

		enum class SituationAction
		{
			kNone,
			kNew,
			kRestore,
			kExit
		};

		// New Situation... (while the page offers it, Request::customSituations) and Exit, centred
		// as the other pages' buttons, then Restore Presets (?) at the row's right end, a link as
		// the Edit situation popup's Remove Situation is, greyed while every preset is as installed.
		// a_escapeTaken: the browser used this frame's Escape, which is otherwise Exit.
		SituationAction DrawSituationButtons(const State& a_state, bool a_escapeTaken)
		{
			constexpr const char* kRestoreHelp =
				"Brings the preset situations back as installed: a removed one returns, empty, and a renamed or edited one "
				"gets its name and conditions back.\nTheir order and nobody's outfits change.";

			const auto& request = a_state.request;
			bool        create = false;
			if (request.customSituations) {
				BeginButtonRow({ { kNewSituation, Theme::HeadingIcon::kStar }, "Exit" });
				BeginItemDisabled(a_state, !request.onSituationCreate);
				create = Theme::Button(Theme::HeadingIcon::kStar, kNewSituation);
				EndItemDisabled();
				ImGui::SameLine();
			} else {
				BeginButtonRow({ "Exit" });
			}
			const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			const bool exit = Theme::Button("Exit") || escape;

			const bool installed = request.restoreChanges.empty();
			BeginItemDisabled(a_state, installed || !request.onRestorePresets);
			bool       helpHovered = false;
			const bool restore = Theme::LineHelpLink("Restore Presets", &helpHovered);
			bool       hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			EndItemDisabled();
			if (installed && FocusStandIn("Restore Presets")) {
				hovered = true;
			}
			// After EndDisabled, so the tooltip is not drawn faded.
			if (installed && hovered) {
				Theme::Tooltip(kAsInstalled);
			} else if (helpHovered) {
				DrawHelpTooltip(kRestoreHelp);
			}

			if (create) {
				return SituationAction::kNew;
			}
			if (restore) {
				return SituationAction::kRestore;
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

		// `Remove Town?` (the name after its star), who has outfits on it in this save, and for a
		// preset what Restore Presets brings back; Remove / Cancel. Returns true on Remove.
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
			const auto  situation = SituationRun(saved.name.c_str());
			RunLine({ TextRun("Remove "), situation, TextRun("?") });
			if (!saved.holders.empty()) {
				ImGui::TextUnformatted("Used by:");
				for (const auto& holder : saved.holders) {
					ImGui::BulletText("%s", holder.c_str());
				}
			}
			if (!saved.user) {
				ImGui::TextUnformatted(saved.holders.empty() ? "Restore Presets can bring it back." : "Restore Presets can bring it back, but not their outfits.");
			}
			const bool remove = DrawPopupButtons("Remove");
			ImGui::EndPopup();
			return remove;
		}

		// What Restore Presets changes, one line per situation; Restore / Cancel. Returns true
		// on Restore.
		bool DrawRestorePresetsPopup(const State& a_state)
		{
			if (!BeginModal(kRestorePresetsPopup)) {
				return false;
			}
			ImGui::TextUnformatted("Restore the preset situations?");
			for (const auto& line : a_state.request.restoreChanges) {
				ImGui::BulletText("%s", line.c_str());
			}
			const bool restore = DrawPopupButtons("Restore");
			ImGui::EndPopup();
			return restore;
		}

		// The New situation popup: the name, then Create (Enter with a name typed) / Cancel
		// (Escape). Returns true on Create; the name is SituationForm::newName.
		bool DrawNewSituationPopup(State& a_state)
		{
			if (!BeginModal(kNewSituationPopup)) {
				return false;
			}
			auto& name = a_state.situationForm.newName;
			ImGui::TextUnformatted("Name the new situation.");
			const bool enter = DrawNameField(a_state, name, nullptr, nullptr, "Situation Name");
			BeginButtonRow({ "Create", "Cancel" });
			const bool typed = !Trimmed(name.data()).empty();
			const bool create = ReasonButton("Create", typed ? nullptr : kNeedName) || enter;
			ImGui::SameLine();
			const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
			EndButtonRow();
			if (create || cancel) {
				QueueMenuSound(create ? kOkSound : kCancelSound);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
			return create;
		}

		// The row of `a_kind` the Add place or Add spell popup adds to (SituationForm::formRow) as
		// edited; nullptr once it is gone.
		SituationRules::Row* FormRow(State& a_state, SituationRules::Kind a_kind)
		{
			auto& form = a_state.situationForm;
			if (form.selected < 0 || static_cast<std::size_t>(form.selected) >= form.entries.size() || form.formRow < 0) {
				return nullptr;
			}
			auto&      rows = form.entries[static_cast<std::size_t>(form.selected)].rules.rows;
			const auto index = static_cast<std::size_t>(form.formRow);
			return index < rows.size() && rows[index].kind == a_kind ? &rows[index] : nullptr;
		}

		// The Add spell popup's row checks the player (SituationRules::Target::kPlayer) in another
		// actor's window, so its first line speaks of you.
		bool ChecksPlayer(State& a_state)
		{
			const auto* row = FormRow(a_state, SituationRules::Kind::kSpell);
			return row && row->target == SituationRules::Target::kPlayer && !a_state.request.actorIsPlayer;
		}

		// What the Add spell popup lists from: the actor's spells, with Show player spells those and
		// the player's, or with Show all spells every one.
		std::span<const SituationRules::SpellChoice> OfferedSpells(const State& a_state)
		{
			const auto& form = a_state.situationForm;
			const auto& request = a_state.request;
			if (form.spellShowAll) {
				return request.allSpells;
			}
			return form.spellShowPlayer && !request.actorIsPlayer ? request.spellsWithPlayer : request.spells;
		}

		// The spells the Add spell popup lists (SituationForm::spellShown): those offered whose name
		// holds the search, ASCII case ignored; with Show all spells and nothing typed, none.
		void RefreshSpellList(State& a_state)
		{
			auto&                  form = a_state.situationForm;
			const std::string_view search = form.spellSearch.data();
			form.spellShown.clear();
			if (form.spellShowAll && search.empty()) {
				return;
			}
			const auto spells = OfferedSpells(a_state);
			for (std::size_t i = 0; i < spells.size(); ++i) {
				if (ExampleMod::ContainsIgnoringCase(spells[i].spell.ref.name, search)) {
					form.spellShown.push_back(i);
				}
			}
		}

		// Opens the popup of the row SituationForm::formRow names: Add place on its first choice
		// (This Location, or This Cell outside any location), or Add spell on the actor's spells
		// with nothing typed or picked.
		// Contract: inside the Edit situation popup, as the popups are drawn there
		// (DrawAddFormPopups).
		void OpenAddFormPopup(State& a_state)
		{
			auto& form = a_state.situationForm;
			if (FormRow(a_state, SituationRules::Kind::kPlace)) {
				form.placePick = 0;
				ImGui::OpenPopup(kAddPlacePopup);
			} else if (FormRow(a_state, SituationRules::Kind::kSpell)) {
				form.spellSearch.fill('\0');
				form.spellShowAll = false;
				form.spellShowPlayer = false;
				form.spellPick.reset();
				RefreshSpellList(a_state);
				a_state.focusNameField = true;  // the search field
				ImGui::OpenPopup(kAddSpellPopup);
			}
		}

		// The Add place popup (docs/custom-situations.md §7): This Location, each Parent Location
		// and This Cell, each with the place's name and a (?) saying what it covers; Add, greyed
		// for a place the row has already, / Cancel (Escape). Returns the place to add on Add.
		std::optional<SituationRules::Place> DrawAddPlacePopup(State& a_state)
		{
			if (!BeginModal(kAddPlacePopup)) {
				return std::nullopt;
			}
			auto&       form = a_state.situationForm;
			const auto& places = a_state.request.places;
			const auto& name = a_state.request.actorName;
			int         shown = 0;  // the choices drawn so far, in request.places.At's order
			const auto  radio = [&form, &shown](const char* a_kind, const SituationRules::Place& a_place, const std::string& a_help) {
				const int  pick = shown++;
				const auto label = std::format("{} ({})", a_kind, SituationRules::FormName(a_place));
				ImGui::PushID(pick);
				if (Theme::RadioButton(label.c_str(), form.placePick == pick)) {
					form.placePick = pick;
					QueueMenuSound(kFocusSound);
				}
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered()) {
					DrawHelpTooltip(a_help);
				}
				ImGui::PopID();
			};
			ImGui::TextUnformatted(std::format("Add the place {} is in.", name).c_str());
			if (places.location) {
				radio("This Location", *places.location, std::format("The named place {} is in.\nMay include multiple rooms or outdoor areas.", name));
				const auto here = SituationRules::FormName(*places.location);
				for (const auto& parent : places.parents) {
					radio("Parent Location", parent, std::format("Includes {} and every place in it.", here));
				}
			}
			if (places.cell) {
				radio("This Cell", *places.cell,
					std::format("The individual area {} is in.\nTypically an entire building interior or a section of the outdoors.", name));
			}
			const auto* picked = places.At(form.placePick);
			const auto* row = FormRow(a_state, SituationRules::Kind::kPlace);
			const bool  listed = picked && row && std::ranges::any_of(row->places, [&picked](const SituationRules::Place& a_place) {
				return SituationRules::SameForm(a_place.ref, picked->ref);
			});
			BeginButtonRow({ "Add", "Cancel" });
			const char* reason = !picked ? "Nothing to add here." : (listed ? "Already in the list." : nullptr);
			const bool  add = ReasonButton("Add", reason);
			ImGui::SameLine();
			const bool cancel = Theme::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
			EndButtonRow();
			if (add || cancel) {
				QueueMenuSound(add ? kOkSound : kCancelSound);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
			return add && picked ? std::optional{ *picked } : std::nullopt;
		}

		// The Add spell popup's search field, underlined with a magnifying glass as the outfit
		// search, `a_width` wide, a clear button on its right end while text is typed. Returns true
		// when the search changed; sets `a_escapeTaken` when Escape cleared it, so it does not also
		// cancel.
		bool DrawSpellSearch(State& a_state, float a_width, bool& a_escapeTaken)
		{
			auto&      search = a_state.situationForm.spellSearch;
			const bool searching = search[0] != '\0';
			a_escapeTaken = searching && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
			const Theme::FieldStyle style{ .look = searching ? Theme::FieldLook::kHighlighted : Theme::FieldLook::kNormal,
				.shape = Theme::FieldShape::kUnderlined,
				.icon = Theme::FieldIcon::kSearch };
			if (a_state.focusNameField) {
				ImGui::SetKeyboardFocusHere();
				a_state.focusNameField = false;
			}
			ImGui::SetNextItemWidth(a_width);
			ImGui::SetNextItemAllowOverlap();  // the clear button sits on its right end
			bool changed = Theme::InputText("##search", search.data(), search.size(), ImGuiInputTextFlags_EscapeClearsAll, "Search by name", style);
			bool clear = a_escapeTaken;
			if (search[0] != '\0') {
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
				search.fill('\0');
				changed = true;
			}
			return changed;
		}

		// The Add spell popup's list: the spells SituationForm::spellShown names, a click picking
		// one, its plugin and FormID on hover to tell the same names apart; past the most it lists,
		// a line saying how many more.
		void DrawSpellList(State& a_state)
		{
			constexpr std::size_t kMaxListed = 100;   // rows listed; more need a longer search
			constexpr float       kRowsTall = 10.0f;  // the list's height in rows, kept as the search narrows

			auto&       form = a_state.situationForm;
			const auto  spells = OfferedSpells(a_state);
			const auto  listed = (std::min)(form.spellShown.size(), kMaxListed);
			const char* hint = form.spellShowAll && form.spellSearch[0] == '\0' ? "Type to search." : "Nothing found.";
			PushListRowSpacing();
			const float rowHeight = Theme::SelectRowHeight() + ImGui::GetStyle().ItemSpacing.y;
			if (Theme::BeginList("spells", FittedListHeight(rowHeight * kRowsTall, rowHeight))) {
				if (listed == 0) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					Theme::TextRow(hint);
					ImGui::PopStyleColor();
				}
				for (std::size_t i = 0; i < listed; ++i) {
					// Contract: an index past the list is skipped: a refresh of the window
					// (SetOutfits) may read the actor's spells again while the popup is open.
					if (form.spellShown[i] >= spells.size()) {
						continue;
					}
					const auto& choice = spells[form.spellShown[i]];
					const auto& spell = choice.spell;
					const bool  picked = form.spellPick && SituationRules::SameForm(form.spellPick->ref, spell.ref);
					ImGui::PushID(static_cast<int>(i));
					if (Theme::SelectRow(spell.ref.name.c_str(), picked)) {
						form.spellPick = spell;
						QueueMenuSound(kFocusSound);
					}
					if (ImGui::IsItemHovered()) {
						Theme::Tooltip(SpellTip(spell, choice.effectLines).c_str());
					}
					ImGui::PopID();
				}
			}
			ImGui::PopStyleVar();
			Theme::EndList();
			if (form.spellShown.size() > kMaxListed) {
				Theme::CountText(std::format("{} more. Type more to narrow.", form.spellShown.size() - kMaxListed).c_str(), false);
			}
		}

		// The Add spell popup's Show player spells, right of Show all spells and greyed while it is
		// ticked; not drawn in the player's own window. Returns true when ticked or unticked.
		bool DrawShowPlayerSpells(State& a_state, const char* a_label)
		{
			auto& form = a_state.situationForm;
			ImGui::SameLine();
			ImGui::BeginDisabled(form.spellShowAll);
			const bool changed = Theme::Checkbox(a_label, &form.spellShowPlayer);
			bool       hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();
			if (form.spellShowAll && FocusStandIn(a_label)) {
				hovered = true;
			}
			// After EndDisabled, so the tooltip is not drawn faded.
			if (hovered) {
				Theme::Tooltip("Also lists spells you know and those currently affecting you.");
			}
			return changed;
		}

		// The Add spell popup (docs/custom-situations.md §7): the search, Show all spells and Show
		// player spells, the spells that match; Add, greyed with none picked or one the row has
		// already, / Cancel (Escape, unless it clears the search). Returns the spell to add on Add.
		std::optional<SituationRules::Spell> DrawAddSpellPopup(State& a_state)
		{
			constexpr float       kWidthInFonts = 18.0f;  // the popup's content width, in font sizes
			constexpr const char* kShowAll = "Show all spells";
			constexpr const char* kShowPlayer = "Show player spells";

			auto&       form = a_state.situationForm;
			const auto& name = a_state.request.actorName;
			const bool  showPlayer = !a_state.request.actorIsPlayer;
			const auto  lead = ChecksPlayer(a_state) ? std::string{ "Add a spell to look for on you." } : std::format("Add a spell to look for on {}.", name);
			// The width is set before the popup begins, the line above and the checkboxes widening
			// it: the list runs past the content into the window's padding in Vanilla and Nordic
			// (Theme::BeginList), which a popup fitted to its content would take in and grow by at
			// every frame. The checkboxes' row is as last drawn, or before that guessed with each
			// box a frame height wide (the skins draw it at most about that).
			const auto& style = ImGui::GetStyle();
			float       options = form.spellOptionsWidth;
			if (options <= 0.0f) {
				for (const char* label : { kShowAll, kShowPlayer }) {
					options += ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
				}
				options += style.ItemSpacing.x;
			}
			const float width = (std::max)({ ImGui::CalcTextSize(lead.c_str()).x, ImGui::GetFontSize() * kWidthInFonts, showPlayer ? options : 0.0f });
			const float windowWidth = width + style.WindowPadding.x * 2.0f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, 0.0f), ImVec2(windowWidth, FLT_MAX));
			if (!BeginModal(kAddSpellPopup)) {
				return std::nullopt;
			}
			ImGui::TextUnformatted(lead.c_str());
			bool        escapeTaken = false;
			bool        changed = DrawSpellSearch(a_state, width, escapeTaken);
			const float rowLeft = ImGui::GetCursorScreenPos().x;
			if (Theme::Checkbox(kShowAll, &form.spellShowAll)) {
				QueueMenuSound(kFocusSound);
				changed = true;
			}
			if (ImGui::IsItemHovered()) {
				Theme::Tooltip(std::format(
					"All spells, powers, potions and scrolls, including those added by mods.\n\n"
					"Off: Spells known by {} and those currently affecting them.",
					name)
						.c_str());
			}
			if (showPlayer) {
				if (DrawShowPlayerSpells(a_state, kShowPlayer)) {
					QueueMenuSound(kFocusSound);
					changed = true;
				}
				form.spellOptionsWidth = ImGui::GetItemRectMax().x - rowLeft;
			}
			if (changed) {
				form.spellPick.reset();
				RefreshSpellList(a_state);
			}
			DrawSpellList(a_state);

			const auto* row = FormRow(a_state, SituationRules::Kind::kSpell);
			const auto& pick = form.spellPick;
			const bool  listed = pick && row && std::ranges::any_of(row->spells, [&pick](const SituationRules::Spell& a_spell) {
				return SituationRules::SameForm(a_spell.ref, pick->ref);
			});
			BeginButtonRow({ "Add", "Cancel" });
			const char* reason = !pick ? "Select a spell." : (listed ? "Already in the list." : nullptr);
			const bool  add = ReasonButton("Add", reason);
			ImGui::SameLine();
			const bool cancel = Theme::Button("Cancel") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escapeTaken);
			EndButtonRow();
			if (add || cancel) {
				QueueMenuSound(add ? kOkSound : kCancelSound);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
			return add ? pick : std::nullopt;
		}

		// Hands the page's edits to onSituationsSave: the names that changed, every row
		// when any changed, and the outfit whose details show, which stays on
		// the preview; with `a_reroll`, a die's click (SituationSave::reroll). Until the handler
		// ends with SetOutfits or SetError the page takes no input.
		// Contract: the handler writes situations.json first and changes no row for a refused
		// or failed write (CLAUDE.md §2.1).
		void RequestSituationsSave(State& a_state, bool a_reroll = false)
		{
			const auto&                  form = a_state.situationForm;
			const auto&                  request = a_state.request;
			CaptureWindow::SituationSave save;
			for (std::size_t i = 0; i < form.entries.size(); ++i) {
				const auto& saved = request.situations[i];
				auto        name = Trimmed(form.entries[i].name.data());
				const bool  rules = RulesChanged(a_state, i);
				if (name != saved.name || rules) {
					save.edits.push_back({ .situation = saved.situation,
						.name = std::move(name),
						.rules = rules ? std::optional{ form.entries[i].rules } : std::nullopt });
				}
			}
			if (OrderChanged(form)) {
				for (const auto entry : form.order) {
					save.order.push_back(request.situations[entry].situation);
				}
			}
			if (RowsChanged(a_state)) {
				for (std::size_t i = 0; i < form.entries.size(); ++i) {
					save.rows.push_back({ .situation = request.situations[i].situation, .row = form.entries[i].row });
				}
				save.dropDefaultPieces = request.assigned[0].pieces && !form.defaultPieces;
			}
			if (IndexOfOutfit(a_state, form.viewedOutfit) >= 0) {
				save.previewPieces = ViewedOutfitPieces(a_state);
			}
			save.reroll = a_reroll;
			a_state.status = Status::kSaving;
			a_state.error.clear();
			a_state.autosaveAt = 0.0;
			a_state.quietSave = true;
			auto onSave = request.onSituationsSave;
			SKSE::GetTaskInterface()->AddTask([onSave, save = std::move(save)] { onSave(save); });
		}

		// Hands the selected situation to onSituationRemove; the page is greyed until the
		// handler ends with SetOutfits or SetError.
		// Contract: the handler writes situations.json first and changes no row for a refused
		// or failed write (CLAUDE.md §2.1).
		// A die's click (DiceOf): the page's edits are saved with it, then another outfit of the
		// row the actor wears, picked at random, goes on. An outfit's details close without a
		// preview of their own: the redress begins the preview again on what they wear.
		void RequestReroll(State& a_state)
		{
			auto& form = a_state.situationForm;
			EndNameEditing(form);
			form.viewedOutfit = 0;
			form.viewedRow = -1;
			RequestSituationsSave(a_state, true);
		}

		void RequestSituationRemove(State& a_state)
		{
			a_state.status = Status::kSaving;
			a_state.error.clear();
			auto onRemove = a_state.request.onSituationRemove;
			SKSE::GetTaskInterface()->AddTask(
				[onRemove, situation = a_state.request.situations[static_cast<std::size_t>(a_state.situationForm.selected)].situation] { onRemove(situation); });
		}

		// Hands the New situation popup's name to onSituationCreate; the page is greyed until the
		// handler ends with SetOutfits or SetError.
		// Contract: the handler writes situations.json first and changes nothing for a refused
		// or failed write (CLAUDE.md §2.1).
		void RequestSituationCreate(State& a_state)
		{
			a_state.status = Status::kSaving;
			a_state.error.clear();
			auto onCreate = a_state.request.onSituationCreate;
			SKSE::GetTaskInterface()->AddTask([onCreate, name = Trimmed(a_state.situationForm.newName.data())] { onCreate(name); });
		}

		void RequestRestorePresets(State& a_state)
		{
			a_state.status = Status::kSaving;
			a_state.error.clear();
			auto onRestore = a_state.request.onRestorePresets;
			SKSE::GetTaskInterface()->AddTask([onRestore] { onRestore(); });
		}

		// Opens `a_popup` over what is saved: at once with nothing to save, else once the save
		// took (DrawSituationPage); a refused save opens nothing and shows its error.
		// Contract: in the page's ID scope, as the popups are drawn there.
		void OpenAfterSave(State& a_state, const char* a_popup)
		{
			if (SaveSituationEdits(a_state)) {
				a_state.popupAfterSave = a_popup;
			} else {
				ImGui::OpenPopup(a_popup);
			}
		}

		// The Add place and Add spell popups over the Edit situation popup, and what they add.
		// An edit, saved as the others are (the autosave waits while a popup is open).
		// Contract: inside the Edit situation popup, where OpenAddFormPopup opens them.
		void DrawAddFormPopups(State& a_state)
		{
			auto& form = a_state.situationForm;
			if (auto place = DrawAddPlacePopup(a_state)) {
				if (auto* row = FormRow(a_state, SituationRules::Kind::kPlace)) {
					row->places.push_back(std::move(*place));
					form.neverWornStale = true;
					ScheduleAutosave(a_state);
				}
			}
			if (auto spell = DrawAddSpellPopup(a_state)) {
				if (auto* row = FormRow(a_state, SituationRules::Kind::kSpell)) {
					row->spells.push_back(std::move(*spell));
					form.neverWornStale = true;
					ScheduleAutosave(a_state);
				}
			}
		}

		// The Edit situation popup's right column: `Conditions` (with the Applies help) over the
		// condition editor (DrawConditionEditor) in an area that scrolls to keep the whole popup within
		// kMaxWindowHeightRatio of the screen. Returns the area's height as shown; sets
		// `a_addForm` as DrawConditionEditor does.
		float DrawConditionsColumn(State& a_state, SituationRules::Rules& a_rules, bool& a_addForm)
		{
			auto& form = a_state.situationForm;
			Theme::LabelText("Conditions");
			if (Theme::LabelHelpMark()) {
				DrawHelpTooltip(kConditionsHelp);
			}
			const float areaHeight = ImGui::GetIO().DisplaySize.y * kMaxWindowHeightRatio - form.editorOtherHeight;
			if (Theme::BeginScrollArea("conditions", (std::max)(ImGui::GetFrameHeight(), areaHeight))) {
				if (DrawConditionEditor(a_state, a_rules, a_addForm)) {
					form.neverWornStale = true;
					ScheduleAutosave(a_state);
				}
			}
			Theme::EndScrollArea();
			return ImGui::GetItemRectSize().y;
		}

		// The Edit situation popup (docs/situation-management-ui.md §5, docs/custom-situations.md
		// §7), centred as its height changes until dragged: `Edit` and the situation's name as its
		// heading (the name as last entered, not while it is typed); the situation's settings
		// (DrawSituationSettings) as wide as the page's right pane, and while the page offers the
		// editor (Request::customSituations), for every situation but the Default, the conditions
		// (DrawConditionsColumn) in a column of their own beside them, as wide as the editor's
		// widest row (EditorContentWidth); then Done, with Remove Situation at the row's
		// right end (not for the Default). A condition clicked in the Applies text is lit and
		// scrolled to in the editor. Escape is Done unless it closed a dropdown or a popup over
		// it. Its edits are saved once it closes (TakeAutosaveDue waits for it). Returns true on
		// Remove, which closes it.
		bool DrawEditSituationPopup(State& a_state)
		{
			constexpr const char* kDone = "Done";

			if (!ImGui::IsPopupOpen(kEditSituationPopup)) {
				return false;
			}
			auto&       form = a_state.situationForm;
			const bool  conditions = form.selected > 0 && a_state.request.customSituations;  // the editor's column
			const float columns = conditions ? form.paneWidth + form.columnGap + EditorContentWidth(a_state.request) : form.paneWidth;
			const float windowWidth = columns + ImGui::GetStyle().WindowPadding.x * 2.0f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, 0.0f), ImVec2(windowWidth, FLT_MAX));
			if (!BeginModal(kEditSituationPopup, &form.editorCentred)) {
				return false;
			}
			if (form.selected < 0 || static_cast<std::size_t>(form.selected) >= form.entries.size()) {
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return false;
			}
			if (ImGui::IsWindowAppearing()) {
				form.editorFocus = -1;
				form.editorFocusFrom = ImGui::GetTime();
				form.editorScroll = 0;
				form.editorFocusHeight = 0.0f;
				form.editorPopupAbove = false;
				form.editorCentred = true;
				form.editorOtherHeight = 0.0f;
				form.checkedRows.clear();  // asked again at once, nothing coloured until answered
				form.conditionsHeld.clear();
				// Contract: ImGui 1.92.7 centres the popup (BeginModal's pivot) at the first frame
				// it is shown, at the size its content had the frame before. The scroll area learns
				// its height a frame after it is made, so the popup stays hidden one frame more
				// than ImGui's own, or the first opening is centred too short and grows downwards.
				ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems = 2;
			}
			// Before anything is drawn: a dropdown that Escape closed is gone by now.
			const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !form.editorPopupAbove && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

			const auto selected = static_cast<std::size_t>(form.selected);
			auto&      entry = form.entries[selected];
			if (selected > 0) {
				CheckConditions(a_state, entry.rules);
			}
			const auto                              title = Trimmed(form.nameEditing == form.selected ? entry.nameBefore.c_str() : entry.name.data());
			const std::array<Theme::SubtitleRun, 2> heading{ { TextRun("Edit "), SituationRun(title.c_str(), selected == 0) } };
			Theme::HeadingRunText(heading);
			ImGui::PushID(form.selected);
			bool  addForm = false;
			float areaShown = 0.0f;
			if (!conditions) {
				DrawSituationSettings(a_state, selected);  // no link to click: the Default's, or greyed
			} else if (BeginColumns("edit", 2, form.paneWidth)) {
				NextColumn();
				if (const int clicked = DrawSituationSettings(a_state, selected); clicked >= 0) {
					QueueMenuSound(kFocusSound);
					form.editorFocus = clicked;
					form.editorFocusFrom = ImGui::GetTime();
					form.editorScroll = 1;
				}
				NextColumn();
				ResetCellBaseline();
				areaShown = DrawConditionsColumn(a_state, entry.rules, addForm);
				EndColumns();
			}
			ImGui::PopID();
			if (addForm) {
				QueueMenuSound(kOkSound);
				OpenAddFormPopup(a_state);
			}
			DrawAddFormPopups(a_state);

			BeginButtonRow({ kDone });
			const bool done = Theme::Button(kDone);
			const bool remove = selected > 0 && Theme::LineLink("Remove Situation");
			EndButtonRow();
			if (done || escape) {
				QueueMenuSound(done ? kOkSound : kCancelSound);
			}
			if (done || escape || remove) {
				ImGui::CloseCurrentPopup();
			}
			form.editorPopupAbove = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			// The popup fits its content: its height is the content's bottom and the padding under it.
			const auto* window = ImGui::GetCurrentWindow();
			form.editorOtherHeight = window->DC.CursorMaxPos.y - window->Pos.y + ImGui::GetStyle().WindowPadding.y - areaShown;
			ImGui::EndPopup();
			return remove;
		}

		// The page's popups, each opened by the page in its own ID scope. Returns true when the
		// Edit situation popup's Remove Situation was pressed, which the page answers with Remove's popup.
		bool DrawSituationPopups(State& a_state)
		{
			if (DrawRemoveSituationPopup(a_state)) {
				RequestSituationRemove(a_state);
			}
			if (DrawRestorePresetsPopup(a_state)) {
				RequestRestorePresets(a_state);
			}
			if (DrawNewSituationPopup(a_state)) {
				RequestSituationCreate(a_state);
			}
			return DrawEditSituationPopup(a_state);
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
		const auto viewedFrom = keyOf(form.viewedRow);

		std::vector<SituationEntry> entries(request.situations.size());
		form.selected = -1;
		form.viewedRow = -1;
		for (std::size_t i = 0; i < entries.size(); ++i) {
			const auto& info = request.situations[i];
			auto&       entry = entries[i];
			CopyToField(entry.name, info.name);
			entry.row = request.assigned[i].row;
			entry.rules = info.rules;
			for (std::size_t j = 0; j < a_before.size() && j < form.entries.size(); ++j) {
				if (a_before[j].situation == info.situation) {
					entry.folded = form.entries[j].folded;
				}
			}
			if (selected && info.situation == *selected) {
				form.selected = static_cast<int>(i);
			}
			if (viewedFrom && info.situation == *viewedFrom) {
				form.viewedRow = static_cast<int>(i);
			}
		}
		form.entries = std::move(entries);
		form.order.clear();
		for (std::size_t i = 1; i < form.entries.size(); ++i) {
			form.order.push_back(i);  // as saved: request.situations' order
		}
		form.neverWornStale = true;
		form.defaultPieces = !request.assigned.empty() && request.assigned[0].pieces;
		form.nameEditing = -1;
		form.dragHover = -1;
	}

	void SelectSituation(State& a_state, Situations::Situation a_situation)
	{
		auto&       form = a_state.situationForm;
		const auto& situations = a_state.request.situations;
		for (std::size_t i = 0; i < situations.size() && i < form.entries.size(); ++i) {
			if (situations[i].situation == a_situation) {
				form.selected = static_cast<int>(i);
				form.scrollToSelected = true;
				a_state.popupAfterSave = kEditSituationPopup;  // DrawSituationPage opens it
				return;
			}
		}
	}

	bool HasSituationEdits(const State& a_state)
	{
		for (std::size_t i = 0; i < a_state.situationForm.entries.size(); ++i) {
			if (RulesChanged(a_state, i)) {
				return true;
			}
		}
		return NamesChanged(a_state) || RowsChanged(a_state) || OrderChanged(a_state.situationForm);
	}

	bool SaveSituationEdits(State& a_state)
	{
		EndNameEditing(a_state.situationForm);
		if (!a_state.request.onSituationsSave || !HasSituationEdits(a_state)) {
			return false;
		}
		RequestSituationsSave(a_state);
		return true;
	}

	std::string EditedSituationsText(const State& a_state, std::uint32_t a_outfit)
	{
		const auto& form = a_state.situationForm;
		std::string text;
		for (const auto i : form.order) {
			if (OutfitRows::Holds(form.entries[i].row, a_outfit)) {
				text += std::format("{}{}", text.empty() ? "For " : ", ", Trimmed(form.entries[i].name.data()));
			}
		}
		return text;
	}

	std::vector<std::size_t> ViewedOutfitPieces(const State& a_state)
	{
		const auto& form = a_state.situationForm;
		const int   viewed = IndexOfOutfit(a_state, form.viewedOutfit);
		if (viewed < 0) {
			return {};
		}
		const auto& pieces = OutfitAt(a_state, viewed).pieces;
		const auto  edited = EditedRow(form);
		return edited >= 0 ? WornOf(a_state, pieces, OutfitRows::ExcludedOf(form.entries[static_cast<std::size_t>(edited)].row, form.viewedOutfit)) : pieces;
	}

	void OfferOutfitDrag(const OutfitPayload& a_payload, const char* a_label, Theme::Grip a_grip, const OutfitIcons::Icon* a_icon)
	{
		if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			return;
		}
		ImGui::SetDragDropPayload(kOutfitPayload, &a_payload, sizeof(a_payload));
		const auto icon = a_icon ? RowIconOf(*a_icon) : Theme::RowIcon{};
		Theme::OutfitDragGhost(a_label, a_grip, a_icon ? &icon : nullptr);
		ImGui::EndDragDropSource();
	}

	// The Situation List on the left; on the right the outfit list or an outfit's details
	// (ShowsOutfitView); a situation's settings in the Edit situation popup, opened by its
	// heading's Edit. Every change is an edit, saved shortly after the last one, as the page is
	// left, or once that popup closes; Remove and Restore Presets save them, then write after
	// their popups. The preview shows the Dress page's ticks, or the outfit whose details show.
	void DrawSituationPage(State& a_state)
	{
		auto& form = a_state.situationForm;
		if (a_state.popupAfterSave && a_state.status == Status::kEditing) {
			if (a_state.error.empty()) {
				ImGui::OpenPopup(a_state.popupAfterSave);
			}
			a_state.popupAfterSave = nullptr;
		}
		form.editing = ImGui::IsPopupOpen(kEditSituationPopup);
		EndInactiveNameEdit(a_state);
		BeginPageDisabled(a_state);
		bool       escapeTaken = false;
		ListClick  list;
		auto       outfitView = OutfitViewAction::kNone;
		const auto pageWidth = ImGui::GetContentRegionAvail().x;
		if (BeginColumns("situations")) {
			NextColumn();
			list = DrawSituationList(a_state);
			NextColumn();
			ResetCellBaseline();
			form.paneWidth = ImGui::GetContentRegionAvail().x;
			form.columnGap = pageWidth - form.paneWidth * 2.0f;
			const int viewed = IndexOfOutfit(a_state, form.viewedOutfit);  // after the Situation List, whose clicks change it
			if (ShowsOutfitView(viewed)) {
				outfitView = DrawOutfitView(a_state, viewed, escapeTaken);
			} else {
				escapeTaken = DrawOutfitPane(a_state, viewed);
			}
			EndColumns();
		}
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		const auto action = DrawSituationButtons(a_state, escapeTaken);
		EndButtonRow();
		EndPageDisabled();
		// While a write runs (Status::kSaving) only Escape, as Back or Exit, gets past
		// BeginDisabled; it does nothing then, as the greyed buttons do nothing.
		if (a_state.status != Status::kEditing) {
			DrawSituationPopups(a_state);
			return;
		}

		if (list.dice) {
			QueueMenuSound(kOkSound);
			RequestReroll(a_state);
			DrawSituationPopups(a_state);
			return;
		}
		if (outfitView == OutfitViewAction::kBack) {
			QueueMenuSound(kCancelSound);
			CloseOutfitView(a_state);
		} else if (outfitView == OutfitViewAction::kOpenWardrobe) {
			QueueMenuSound(kOkSound);
			RequestLeave(a_state, { .page = Page::kManage, .manageOutfit = form.viewedOutfit });
		}
		if (list.edit >= 0) {
			QueueMenuSound(kOkSound);
			form.selected = list.edit;
			OpenAfterSave(a_state, kEditSituationPopup);
		}
		switch (action) {
		case SituationAction::kNew:
			QueueMenuSound(kOkSound);
			form.newName.fill('\0');
			a_state.error.clear();
			a_state.focusNameField = true;
			OpenAfterSave(a_state, kNewSituationPopup);
			break;
		case SituationAction::kRestore:
			QueueMenuSound(kOkSound);
			OpenAfterSave(a_state, kRestorePresetsPopup);
			break;
		case SituationAction::kExit:
			RequestLeave(a_state, { .exit = true });
			break;
		case SituationAction::kNone:
			break;
		}
		if (DrawSituationPopups(a_state)) {
			QueueMenuSound(kOkSound);
			OpenAfterSave(a_state, kRemoveSituationPopup);  // the edits made in the popup saved first
		}
		// Contract: not while a name is typed: a save would write it half typed, and SetOutfits
		// would replace the field being typed in.
		if (form.nameEditing < 0 && TakeAutosaveDue(a_state)) {
			SaveSituationEdits(a_state);
		}
	}
}
