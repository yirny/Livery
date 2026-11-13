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
//                     has hovered it a while
//   Situation List  - the left pane: a heading per situation (a click or Edit selects, its
//                     chevron folds, the die of the row worn puts on another of its outfits)
//                     over the outfits it shows; a click on an outfit row of either pane shows
//                     its details and puts it on the preview
//   Condition editor - a user's situation's conditions (docs/custom-situations.md §7): a line
//                     per row with Not, its kind and Remove, its values, times, places or spells
//                     under it; Add Condition..., Apply in combat, the Applies line
//   Right pane      - the outfit list with what a drag does under it; the selected situation's
//                     details: name, conditions, Random Outfit (for #{Name}), Show
//                     Outfit List with Remove; or an outfit's details with Open in Wardrobe and Back, its
//                     pieces ticked as the row it was clicked under wears them
//   Buttons, popups - New Situation..., Exit, Restore Presets (?); the New situation, Remove's and
//                     Restore Presets' popups, each handing its write to the Request's handler;
//                     the Add place and Add spell popups, edits of the selected situation's Place
//                     or Spell row
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
		constexpr const char* kAddPlacePopup = "Add place";
		constexpr const char* kAddSpellPopup = "Add spell";
		constexpr const char* kNewSituation = "New Situation...";
		// Reasons a button is greyed out, shown on hover.
		constexpr const char* kAsInstalled = "Every preset situation is as installed.";
		// The name a situation gets when neither the one typed nor the one before can be used.
		constexpr const char* kUnnamed = "Unnamed Situation";
		// A line in an outfit row's place under a heading.
		constexpr const char* kPiecesLine = "Items not in the Wardrobe";

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

		// Entry `a_index` is a user's situation whose conditions differ from situations.json.
		bool RulesChanged(const State& a_state, std::size_t a_index)
		{
			const auto& saved = a_state.request.situations[a_index];
			return saved.user && a_state.situationForm.entries[a_index].rules != saved.rules;
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
			float       rows = 0.0f;
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
				if (DrawOutfitRow(a_state, a_state.situationBrowser, index, viewed, k % 2 == 1, Theme::Grip::kName, false)) {
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

		// Situation `a_entry`'s heading, then, unless folded, what it shows. A click left of the
		// chevron selects it and shows its details, closing an outfit's, or unselects it when its
		// details show; Edit always selects it and shows them; the chevron folds it. Its die and
		// count show with random on alone; the die is a button as DiceOf says. Only random on
		// with something to show has the chevron; else it is never folded (random off shows its
		// one outfit, a drop shows what it got). With nothing to show its name is greyed. A drop
		// on the heading, its rule included, adds at the front. `a_dragHovered` becomes the entry
		// while a drag hovers its folded heading, which opens once DrawSituationList has seen
		// that for kDragExpandDelay. Returns whether its die was clicked.
		bool DrawSituationEntry(State& a_state, std::size_t a_entry, int& a_dragHovered)
		{
			auto&      form = a_state.situationForm;
			auto&      entry = form.entries[a_entry];
			const int  index = static_cast<int>(a_entry);
			const bool shows = ShowsAnything(form, a_entry);
			const bool foldable = shows && entry.row.random;
			if (!foldable) {
				entry.folded = false;
			}
			const auto  dice = entry.row.random ? DiceOf(a_state, a_entry) : Dice{};
			const float top = ImGui::GetCursorScreenPos().y;
			const auto  click = Theme::SituationHeading(entry.name.data(),
				{ .count = entry.row.random ? static_cast<int>(entry.row.outfits.size()) : -1,
					.random = entry.row.random,
					.diceButton = dice.button,
					.diceTip = dice.tip.empty() ? nullptr : dice.tip.c_str(),
					.open = !entry.folded,
					.foldable = foldable,
					.selected = form.selected == index && form.viewedOutfit == 0,  // an outfit's details show in its place
					.dim = !shows,
					.icon = a_entry == 0 ? Theme::HeadingIcon::kNone : Theme::HeadingIcon::kStar });  // the Default is no situation: no star
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
					DropOnSituation(a_state, a_entry, 0, *dropped);
				}
			}
			if (!entry.folded) {
				DrawShownOutfits(a_state, a_entry);
			}
			return click == Theme::HeadingClick::kDice;
		}

		// The left pane: the Situation List (?) label over a heading per situation, the Default
		// first. Returns whether a heading's die was clicked.
		bool DrawSituationList(State& a_state)
		{
			auto&       form = a_state.situationForm;
			const float paneTop = ImGui::GetCursorPosY();
			Theme::LabelText("Situation List");
			if (Theme::LabelHelpMark()) {
				DrawHelpTooltip(std::format(
					"Each situation lists the outfits {} can wear there.\nWhen several apply, the most specific one with an "
					"outfit is worn; otherwise, and in combat, the default outfit.\nDrag an outfit onto a situation to add it, between "
					"its outfits to place it, onto another situation to move it, or back to the outfit list to take it off.",
					a_state.request.actorName));
			}
			const float top = ImGui::GetCursorPosY() - paneTop;
			// No gap between rows: the headings and rows hold it themselves (Theme::OutfitRow).
			// Popped before EndList, as the outfit list's spacing is.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
			int  dragHovered = -1;
			bool diceClicked = false;
			if (Theme::BeginList("situations", SituationListHeight(a_state, top))) {
				for (std::size_t i = 0; i < form.entries.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					diceClicked = DrawSituationEntry(a_state, i, dragHovered) || diceClicked;
					ImGui::PopID();
				}
			}
			ImGui::PopStyleVar();
			Theme::EndList();
			if (form.dragHover != dragHovered) {
				form.dragHover = dragHovered;
				form.dragHoverSince = ImGui::GetTime();
			}
			return diceClicked;
		}

		// --- Condition editor -------------------------------------------------

		// The values of `a_row`'s kind as checkboxes, side by side while the pane has room.
		// Returns true when one was ticked or unticked.
		bool DrawRowValues(SituationRules::Row& a_row)
		{
			const auto& style = ImGui::GetStyle();
			const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			const auto  values = SituationRules::InfoOf(a_row.kind).values;
			bool        changed = false;
			for (std::size_t i = 0; i < values.size(); ++i) {
				const auto& value = values[i];
				if (i > 0) {
					const float width = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(value.label).x;
					if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right) {
						ImGui::SameLine();
					}
				}
				bool ticked = (a_row.values & value.bit) != 0;
				if (Theme::Checkbox(value.label, &ticked)) {
					a_row.values ^= value.bit;
					changed = true;
				}
			}
			return changed;
		}

		// A dropdown of the times of day, `a_minutes` (after midnight) its choice. Returns true
		// when another was chosen.
		bool DrawTimeDropdown(const char* a_id, std::uint16_t& a_minutes)
		{
			constexpr std::uint16_t kTimeStep = 30;  // minutes between the times offered

			const Theme::FieldStyle style{};
			const auto              current = SituationRules::TimeText(a_minutes);
			ImGui::SetNextItemWidth(Theme::DropdownWidth("12:30 AM", style));
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

		enum class ConditionEdit
		{
			kNone,
			kChanged,
			kRemove,
			kAddForm  // a Place row's Add Place... or a Spell row's Add Spell... pressed
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
			for (const std::span<const SituationRules::SpellChoice> list : { std::span<const SituationRules::SpellChoice>(a_request.spells), a_request.allSpells }) {
				if (const auto found = std::ranges::find_if(list, same); found != list.end()) {
					return found->effectLines;
				}
			}
			return none;
		}

		// A Place or Spell row's forms, a line each with Remove at its right end, one whose plugin
		// is not loaded greyed and saying so on hover, a spell's effects on hover; then Add
		// Place... or Add Spell.... Returns true when one was removed; sets `a_add` when the Add
		// link was pressed.
		bool DrawFormList(const CaptureWindow::Request& a_request, SituationRules::Row& a_row, bool& a_add)
		{
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
				} else if (a_row.kind == SituationRules::Kind::kSpell && ImGui::IsItemHovered()) {
					Theme::Tooltip(SpellTip(form, EffectLinesOf(a_request, form)).c_str());
				}
				if (Theme::LineLink("Remove")) {
					removed = i;
				}
				ImGui::PopID();
			}
			if (removed) {
				forms.erase(forms.begin() + static_cast<std::ptrdiff_t>(*removed));
			}
			a_add = Theme::LabelLink(a_row.kind == SituationRules::Kind::kPlace ? "Add Place..." : "Add Spell...", Theme::LinkLine::kNext);
			return removed.has_value();
		}

		// One condition row: Not, its kind (what it means on hover, the actor's name in it) and
		// Remove at the line's right end, then indented under it its values, places or spells, or
		// From and to for Time of day.
		ConditionEdit DrawConditionRow(const CaptureWindow::Request& a_request, SituationRules::Row& a_row)
		{
			const auto& info = SituationRules::InfoOf(a_row.kind);
			auto        edit = ConditionEdit::kNone;
			if (Theme::Checkbox("Not", &a_row.negated)) {
				edit = ConditionEdit::kChanged;
			}
			if (ImGui::IsItemHovered()) {
				Theme::Tooltip("Holds while this is not so.");
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(info.label);
			if (ImGui::IsItemHovered()) {
				Theme::Tooltip(std::vformat(info.tip, std::make_format_args(a_request.actorName)).c_str());
			}
			if (Theme::LineLink("Remove")) {
				edit = ConditionEdit::kRemove;
			}
			const float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;  // under the kind's name
			ImGui::Indent(indent);
			bool changed = false;
			bool addForm = false;
			if (!info.values.empty()) {
				changed = DrawRowValues(a_row);
			} else if (SituationRules::HasForms(a_row.kind)) {
				changed = DrawFormList(a_request, a_row, addForm);
			} else if (a_row.kind == SituationRules::Kind::kTimeOfDay) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("From");
				ImGui::SameLine();
				changed = DrawTimeDropdown("##from", a_row.from);
				ImGui::SameLine();
				ImGui::TextUnformatted("to");
				ImGui::SameLine();
				changed = DrawTimeDropdown("##to", a_row.to) || changed;
			}
			ImGui::Unindent(indent);
			if (edit == ConditionEdit::kNone && addForm) {
				return ConditionEdit::kAddForm;
			}
			return edit == ConditionEdit::kNone && changed ? ConditionEdit::kChanged : edit;
		}

		// Add Condition...: a dropdown of the kinds, closed by a choice; returns the one chosen.
		std::optional<SituationRules::Kind> DrawAddCondition()
		{
			constexpr const char* kAddCondition = "Add Condition...";

			const Theme::FieldStyle style{ .previewIsHint = true };
			ImGui::SetNextItemWidth(Theme::DropdownWidth(kAddCondition, style));
			const bool                          open = Theme::BeginCombo("##addCondition", ImGuiComboFlags_HeightLargest, style);
			std::optional<SituationRules::Kind> added;
			if (open) {
				for (const auto& info : SituationRules::kKinds) {
					bool chosen = false;
					if (Theme::DropdownRow(info.label, &chosen, false)) {
						added = info.kind;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			Theme::EndCombo(open, kAddCondition, style);
			return added;
		}

		// Apply in combat, ticked and greyed while an In combat condition needs it.
		bool DrawApplyInCombat(const State& a_state, SituationRules::Rules& a_rules)
		{
			const bool forced = !a_rules.applyInCombat && SituationRules::AppliesInCombat(a_rules);
			bool       ticked = forced || a_rules.applyInCombat;
			BeginItemDisabled(a_state, forced);
			const bool changed = Theme::Checkbox("Apply in combat", &ticked);
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			EndItemDisabled();
			// After EndDisabled, so the tooltip is not drawn faded.
			if (hovered) {
				Theme::Tooltip(forced ? "On while the situation has an In combat condition." :
										"The situation can apply during a fight.\n\nOff: In combat, the default outfit is worn.");
			}
			if (changed) {
				a_rules.applyInCombat = ticked;
			}
			return changed;
		}

		// A user's situation's conditions as edited: a line per row (DrawConditionRow), Add
		// Condition..., Apply in combat, then the Applies line they make. Returns true on a change;
		// sets `a_addForm` when a Place or Spell row's Add link was pressed, that row in
		// SituationForm::formRow.
		bool DrawConditionEditor(State& a_state, SituationRules::Rules& a_rules, bool& a_addForm)
		{
			Theme::LabelText("Conditions");
			if (Theme::LabelHelpMark()) {
				DrawHelpTooltip("The situation applies while all its conditions hold.\nTick several values of one condition for any of them.");
			}
			bool                       changed = false;
			std::optional<std::size_t> removed;
			for (std::size_t i = 0; i < a_rules.rows.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				const auto edit = DrawConditionRow(a_state.request, a_rules.rows[i]);
				ImGui::PopID();
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
			}
			if (const auto kind = DrawAddCondition()) {
				a_rules.rows.push_back({ .kind = *kind });
				changed = true;
				// A Place or Spell row starts empty, so its Add link is pressed for the user.
				if (SituationRules::HasForms(*kind)) {
					a_state.situationForm.formRow = static_cast<int>(a_rules.rows.size() - 1);
					a_addForm = true;
				}
			}
			changed = DrawApplyInCombat(a_state, a_rules) || changed;
			Theme::LabelText("Applies");
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			DrawFieldText(SituationRules::Text(a_rules).c_str());
			ImGui::PopStyleColor();
			if (changed) {
				QueueMenuSound(kFocusSound);
			}
			return changed;
		}

		// --- Right pane -------------------------------------------------------

		enum class RightPane
		{
			kOutfitList,
			kSituation,  // the selected situation's details
			kOutfit      // the details of the outfit clicked last
		};

		// What the right pane shows: the outfit list while a drag from the Situation List is on;
		// else the outfit clicked last (`a_viewed`, its index into request.existingOutfits, -1
		// for none), until Back; else the selected situation, until Show Outfit List; else the
		// outfit list.
		RightPane ShownPane(const State& a_state, int a_viewed)
		{
			const auto& form = a_state.situationForm;
			const auto* dragged = DraggedOutfit();
			if (dragged && dragged->from >= 0) {
				return RightPane::kOutfitList;
			}
			if (a_viewed >= 0) {
				return RightPane::kOutfit;
			}
			return form.selected >= 0 && form.details ? RightPane::kSituation : RightPane::kOutfitList;
		}

		// The outfit list with its own search and sort, its rows dragged as copies and
		// a click on one showing its details (`a_viewed`'s row selected). A drag from the Situation
		// List dropped anywhere on it takes that outfit, or the Default's pieces, off its row.
		// Returns whether Escape cleared the search.
		bool DrawOutfitPane(State& a_state, int a_viewed)
		{
			constexpr float kFrameWidth = 2.0f;  // pixels at 1080p, around the pane while a drop would take an outfit off

			const ImVec2 top = ImGui::GetCursorScreenPos();
			const float  width = ImGui::GetContentRegionAvail().x;
			const auto   controls = DrawBrowserControls(a_state, a_state.situationBrowser);
			const int    clicked = DrawOutfitList(a_state, a_state.situationBrowser, a_viewed, controls.height, true);
			const float  bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
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

		// The settings every actor shares: the name, its label with the situation star (none for
		// the Default, which says what it is under it), and the conditions: a user's situation's
		// in the condition editor, a preset's read only. Returns true when a Place or Spell row's
		// Add link was pressed (SituationForm::formRow).
		bool DrawGlobalSettings(State& a_state, std::size_t a_entry)
		{
			auto&      form = a_state.situationForm;
			auto&      entry = form.entries[a_entry];
			const bool isDefault = a_entry == 0;
			Theme::LabelText("Name", isDefault ? Theme::HeadingIcon::kNone : Theme::HeadingIcon::kStar);
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
			if (a_state.request.situations[a_entry].user) {
				bool addForm = false;
				if (DrawConditionEditor(a_state, entry.rules, addForm)) {
					ScheduleAutosave(a_state);
				}
				return addForm;
			}
			Theme::LabelText("Conditions");
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			if (isDefault) {
				DrawDefaultConditions();
			} else {
				DrawFieldText(a_state.request.situations[a_entry].conditions.c_str());
			}
			ImGui::PopStyleColor();
			return false;
		}

		enum class DetailsAction
		{
			kNone,
			kShowList,
			kRemove,
			kAddForm
		};

		// The selected situation's details as edited: its name and conditions,
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
			const bool addForm = DrawGlobalSettings(a_state, selected);

			Theme::SectionGap();  // a field of its own, with no label
			bool              random = entry.row.random;
			const std::string randomLabel = std::format("Random Outfit (for {})", name);
			if (Theme::Checkbox(randomLabel.c_str(), &random)) {
				QueueMenuSound(kFocusSound);
				OutfitRows::SetRandom(entry.row, random);  // off keeps the last pick on
				ScheduleAutosave(a_state);
				// An outfit the Default wears with random off drops its pieces, as a drop does.
				if (selected == 0 && !random && entry.row.chosen != 0) {
					form.defaultPieces = false;
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				DrawHelpTooltip(std::format(
					"On: Each time this situation begins, {0} wears a random outfit from the list until it ends.\n\n"
					"Off: {0} wears the last picked outfit. The others stay in the list but are hidden.",
					name));
			}

			BeginButtonRow({ kShowList });
			auto action = Theme::Button(kShowList) ? DetailsAction::kShowList : DetailsAction::kNone;
			if (selected > 0 && Theme::LineLink("Remove")) {
				action = DetailsAction::kRemove;
			}
			ImGui::PopID();
			return addForm ? DetailsAction::kAddForm : action;
		}

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
		// List; its pieces' list scrolls only to keep the whole view
		// within kMaxListHeightRatio of the screen, as every list's column keeps, so the window
		// stays on screen. Escape is Back, and sets `a_escapeTaken` so it is not also Exit.
		// Contract: ShownPane is RightPane::kOutfit; called at the top of the right pane.
		OutfitViewAction DrawOutfitView(State& a_state, int a_viewed, bool& a_escapeTaken)
		{
			const float     spacing = ImGui::GetStyle().ItemSpacing.x;
			const RowButton open{ "Open in Wardrobe", Theme::HeadingIcon::kWardrobe };
			const bool      stacked = ButtonRowWidth({ open, "Back" }) + spacing * 2.0f > ImGui::GetContentRegionAvail().x;
			const float     buttonRow = ImGui::GetStyle().ItemSpacing.y + ButtonRowGap() + Theme::ButtonHeight() +
			                            (stacked ? spacing + Theme::ButtonHeight() : 0.0f);                               // under the pieces' list
			const float     bottom = ImGui::GetCursorScreenPos().y + ImGui::GetIO().DisplaySize.y * kMaxListHeightRatio;  // the view's lowest
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

		// New Situation... and Exit, centred as the other pages' buttons, then Restore Presets (?)
		// at the row's right end, a link as the details' Remove and Restore are, greyed while
		// every preset is as installed. a_escapeTaken: the
		// browser used this frame's Escape, which is otherwise Exit.
		SituationAction DrawSituationButtons(const State& a_state, bool a_escapeTaken)
		{
			constexpr const char* kRestoreHelp =
				"Brings the preset situations back as installed: a removed one returns, empty, and a renamed one "
				"gets its name back.\nNobody's outfits change.";

			const auto& request = a_state.request;
			BeginButtonRow({ { kNewSituation, Theme::HeadingIcon::kStar }, "Exit" });
			BeginItemDisabled(a_state, !request.onSituationCreate);
			const bool create = Theme::Button(Theme::HeadingIcon::kStar, kNewSituation);
			EndItemDisabled();
			ImGui::SameLine();
			const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !a_escapeTaken && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			const bool exit = Theme::Button("Exit") || escape;

			const bool installed = request.restoreChanges.empty();
			BeginItemDisabled(a_state, installed || !request.onRestorePresets);
			bool       helpHovered = false;
			const bool restore = Theme::LineHelpLink("Restore Presets", &helpHovered);
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			EndItemDisabled();
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

		// What the Add spell popup lists from: the actor's spells, or with Show all spells every one.
		std::span<const SituationRules::SpellChoice> OfferedSpells(const State& a_state)
		{
			return a_state.situationForm.spellShowAll ? a_state.request.allSpells : std::span<const SituationRules::SpellChoice>(a_state.request.spells);
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
		// Contract: in the page's ID scope, as the popups are drawn there.
		void OpenAddFormPopup(State& a_state)
		{
			auto& form = a_state.situationForm;
			if (FormRow(a_state, SituationRules::Kind::kPlace)) {
				form.placePick = 0;
				ImGui::OpenPopup(kAddPlacePopup);
			} else if (FormRow(a_state, SituationRules::Kind::kSpell)) {
				form.spellSearch.fill('\0');
				form.spellShowAll = false;
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

		// The Add spell popup (docs/custom-situations.md §7): the search, Show all spells, the
		// spells that match; Add, greyed with none picked or one the row has already, / Cancel
		// (Escape, unless it clears the search). Returns the spell to add on Add.
		std::optional<SituationRules::Spell> DrawAddSpellPopup(State& a_state)
		{
			constexpr float kWidthInFonts = 22.0f;  // the popup's content width, in font sizes

			if (!BeginModal(kAddSpellPopup)) {
				return std::nullopt;
			}
			auto&       form = a_state.situationForm;
			const auto& name = a_state.request.actorName;
			ImGui::TextUnformatted(std::format("Add a spell to look for on {}.", name).c_str());
			// The field sets the popup's width, the list fills it; a long name above widens both.
			const float width = (std::max)(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * kWidthInFonts);
			bool        escapeTaken = false;
			bool        changed = DrawSpellSearch(a_state, width, escapeTaken);
			if (Theme::Checkbox("Show all spells", &form.spellShowAll)) {
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

		// The page's popups, each opened by the page in its own ID scope.
		void DrawSituationPopups(State& a_state)
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
			// An edit, saved as the others are (the autosave waits while a popup is open).
			if (auto place = DrawAddPlacePopup(a_state)) {
				if (auto* row = FormRow(a_state, SituationRules::Kind::kPlace)) {
					row->places.push_back(std::move(*place));
					ScheduleAutosave(a_state);
				}
			}
			if (auto spell = DrawAddSpellPopup(a_state)) {
				if (auto* row = FormRow(a_state, SituationRules::Kind::kSpell)) {
					row->spells.push_back(std::move(*spell));
					ScheduleAutosave(a_state);
				}
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
		form.defaultPieces = !request.assigned.empty() && request.assigned[0].pieces;
		form.details = form.details && form.selected >= 0;
		form.nameEditing = -1;
		form.dragHover = -1;
	}

	void SelectSituation(State& a_state, Situations::Situation a_situation)
	{
		auto&       form = a_state.situationForm;
		const auto& situations = a_state.request.situations;
		for (std::size_t i = 0; i < situations.size() && i < form.entries.size(); ++i) {
			if (situations[i].situation == a_situation) {
				if (form.viewedOutfit != 0) {
					CloseOutfitView(a_state);
				}
				form.selected = static_cast<int>(i);
				form.details = true;
				form.scrollToSelected = true;
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
		return NamesChanged(a_state) || RowsChanged(a_state);
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
		const auto& entries = a_state.situationForm.entries;
		std::string text;
		for (std::size_t i = 1; i < entries.size(); ++i) {
			if (OutfitRows::Holds(entries[i].row, a_outfit)) {
				text += std::format("{}{}", text.empty() ? "For " : ", ", Trimmed(entries[i].name.data()));
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

	// The Situation List on the left; on the right the outfit list, the selected situation or
	// an outfit's details (ShownPane). Every change is an edit, saved shortly after the last one
	// or as the page is left; Remove and Restore Presets save them, then write after their
	// popups. The preview shows the Dress page's ticks, or the outfit whose details show.
	void DrawSituationPage(State& a_state)
	{
		auto& form = a_state.situationForm;
		if (a_state.popupAfterSave && a_state.status == Status::kEditing) {
			if (a_state.error.empty()) {
				ImGui::OpenPopup(a_state.popupAfterSave);
			}
			a_state.popupAfterSave = nullptr;
		}
		EndInactiveNameEdit(a_state);
		BeginPageDisabled(a_state);
		bool escapeTaken = false;
		bool reroll = false;
		auto details = DetailsAction::kNone;
		auto outfitView = OutfitViewAction::kNone;
		if (BeginColumns("situations")) {
			ImGui::TableNextColumn();
			reroll = DrawSituationList(a_state);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			const int viewed = IndexOfOutfit(a_state, form.viewedOutfit);  // after the Situation List, whose clicks change it
			switch (ShownPane(a_state, viewed)) {
			case RightPane::kOutfit:
				outfitView = DrawOutfitView(a_state, viewed, escapeTaken);
				break;
			case RightPane::kSituation:
				details = DrawSituationDetails(a_state);
				break;
			case RightPane::kOutfitList:
				escapeTaken = DrawOutfitPane(a_state, viewed);
				break;
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

		if (reroll) {
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
		if (details == DetailsAction::kShowList) {
			QueueMenuSound(kFocusSound);
			form.details = false;
			form.entries[static_cast<std::size_t>(form.selected)].folded = false;
			form.scrollToSelected = true;
		} else if (details == DetailsAction::kRemove) {
			QueueMenuSound(kOkSound);
			OpenAfterSave(a_state, kRemoveSituationPopup);
		} else if (details == DetailsAction::kAddForm) {
			QueueMenuSound(kOkSound);
			OpenAddFormPopup(a_state);
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
		DrawSituationPopups(a_state);
		// Contract: not while a name is typed: a save would write it half typed, and SetOutfits
		// would replace the field being typed in.
		if (form.nameEditing < 0 && TakeAutosaveDue(a_state)) {
			SaveSituationEdits(a_state);
		}
	}
}
