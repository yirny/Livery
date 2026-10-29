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
//                     chevron folds) over the outfits it shows; a click on an outfit row of
//                     either pane shows its details and puts it on the preview
//   Right pane      - the outfit list with what a drag does under it; the selected situation's
//                     details: name, conditions, Random Outfit (for #{Name}), Show
//                     Outfit List with Remove; or an outfit's details with Edit and Back, its
//                     pieces ticked as the row it was clicked under wears them
//   Buttons, popups - Save Changes, Exit, Restore Presets (?); Remove's and Restore Presets'
//                     popups and the unsaved-changes alert, each handing its write to the
//                     Request's handler
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
		// A line in an outfit row's place under a heading.
		constexpr const char* kPiecesLine = "Items not in the wardrobe";

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
		}

		// A drag from the Situation List dropped on the outfit list: the outfit leaves its row
		// (its `chosen` 0 if it was that), or the Default's pieces go.
		void DropOnOutfitList(State& a_state, const OutfitPayload& a_payload)
		{
			auto& form = a_state.situationForm;
			if (a_payload.from < 0) {
				return;
			}
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
		// the preview the Inventory page's ticks again.
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

		// Situation `a_entry`'s heading, then, unless folded, what it shows. A click left of the
		// chevron selects it and shows its details, closing an outfit's, or unselects it when its
		// details show; Edit always selects it and shows them; the chevron folds it. Its die and
		// count show with random on alone. Only random on with something to show has the
		// chevron; else it is never folded (random off shows its one outfit, a drop shows what it
		// got). With nothing to show its name is greyed. A drop on the heading, its rule included,
		// adds at the front. `a_dragHovered` becomes the entry while a drag hovers its folded
		// heading, which opens once DrawSituationList has seen that for kDragExpandDelay.
		void DrawSituationEntry(State& a_state, std::size_t a_entry, int& a_dragHovered)
		{
			auto&      form = a_state.situationForm;
			auto&      entry = form.entries[a_entry];
			const int  index = static_cast<int>(a_entry);
			const bool shows = ShowsAnything(form, a_entry);
			const bool foldable = shows && entry.row.random;
			if (!foldable) {
				entry.folded = false;
			}
			const float top = ImGui::GetCursorScreenPos().y;
			const auto  click = Theme::SituationHeading(entry.name.data(),
				{ .count = entry.row.random ? static_cast<int>(entry.row.outfits.size()) : -1,
					.random = entry.row.random,
					.open = !entry.folded,
					.foldable = foldable,
					.selected = form.selected == index && form.viewedOutfit == 0,  // an outfit's details show in its place
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
					DropOnSituation(a_state, a_entry, 0, *dropped);
				}
			}
			if (!entry.folded) {
				DrawShownOutfits(a_state, a_entry);
			}
		}

		// The left pane: the Situation List (?) label over a heading per situation, the Default
		// first.
		void DrawSituationList(State& a_state)
		{
			auto&       form = a_state.situationForm;
			const float paneTop = ImGui::GetCursorPosY();
			Theme::LabelText("Situation List");
			if (Theme::LabelHelpMark()) {
				DrawHelpTooltip(std::format(
					"Each situation lists the outfits {} can wear there. When several apply, the most specific one with an "
					"outfit is worn; otherwise, and in combat, the Default. Drag an outfit onto a situation to add it, between "
					"its outfits to place it, onto another situation to move it, or back to the outfit list to take it off.",
					a_state.request.actorName));
			}
			const float top = ImGui::GetCursorPosY() - paneTop;
			// No gap between rows: the headings and rows hold it themselves (Theme::OutfitRow).
			// Popped before EndList, as the outfit list's spacing is.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
			int dragHovered = -1;
			if (Theme::BeginList("situations", SituationListHeight(a_state, top))) {
				for (std::size_t i = 0; i < form.entries.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					DrawSituationEntry(a_state, i, dragHovered);
					ImGui::PopID();
				}
			}
			ImGui::PopStyleVar();
			Theme::EndList();
			if (form.dragHover != dragHovered) {
				form.dragHover = dragHovered;
				form.dragHoverSince = ImGui::GetTime();
			}
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

		// kPaneButtonGap, then the cursor placed so a row of buttons with these labels is
		// centred in the pane.
		void BeginPaneButtonRow(std::initializer_list<const char*> a_labels)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kPaneButtonGap * Theme::Scale());
			CentreButtonRow(a_labels);
		}

		// The settings every actor shares: the name, its label with the situation star (read
		// only for the Default, which says so on hover), and the conditions, read only.
		void DrawGlobalSettings(State& a_state, std::size_t a_entry)
		{
			constexpr const char* kDefaultName = "The Default's name cannot be changed.";

			auto&      form = a_state.situationForm;
			auto&      entry = form.entries[a_entry];
			const bool isDefault = a_entry == 0;
			Theme::LabelText("Name", true, true);
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
				Theme::Tooltip(kDefaultName);
			}
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
			DrawGlobalSettings(a_state, selected);

			Theme::SectionGap();  // a field of its own, with no label
			bool              random = entry.row.random;
			const std::string randomLabel = std::format("Random Outfit (for {})", name);
			if (Theme::Checkbox(randomLabel.c_str(), &random)) {
				QueueMenuSound(kFocusSound);
				OutfitRows::SetRandom(entry.row, random);  // off keeps the last pick on
				// An outfit the Default wears with random off drops its pieces, as a drop does.
				if (selected == 0 && !random && entry.row.chosen != 0) {
					form.defaultPieces = false;
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				DrawHelpTooltip(std::format(
					"On: one outfit of the list is picked at random each time this situation begins for {}, and worn until it "
					"ends. Off: only the outfit shown is worn, the last one picked; the others stay on the list, hidden.",
					name));
			}

			BeginPaneButtonRow({ kShowList });
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
		// (read only from the outfit list, or once the row lost it); then Edit and Back centred.
		// It grows with the pieces, moving the page's buttons when taller than the Situation
		// List; its pieces' list scrolls only to keep the whole view
		// within kMaxListHeightRatio of the screen, as every list's column keeps, so the window
		// stays on screen. Escape is Back, and sets `a_escapeTaken` so it is not also Exit.
		// Contract: ShownPane is RightPane::kOutfit; called at the top of the right pane.
		OutfitViewAction DrawOutfitView(State& a_state, int a_viewed, bool& a_escapeTaken)
		{
			const float buttonRow = ImGui::GetStyle().ItemSpacing.y + kPaneButtonGap * Theme::Scale() + Theme::ButtonHeight();  // under the pieces' list
			const float bottom = ImGui::GetCursorScreenPos().y + ImGui::GetIO().DisplaySize.y * kMaxListHeightRatio;            // the view's lowest
			ImGui::PushID("outfitView");
			auto&      form = a_state.situationForm;
			const auto edited = EditedRow(form);
			auto*      row = edited >= 0 ? &form.entries[static_cast<std::size_t>(edited)].row : nullptr;
			auto       excluded = row ? OutfitRows::ExcludedOf(*row, form.viewedOutfit) : std::vector<std::uint32_t>{};
			if (DrawOutfitSummary(a_state, a_viewed, HeldRowsText(a_state, form.viewedOutfit), bottom - buttonRow, row ? &excluded : nullptr)) {
				OutfitRows::SetExcluded(*row, form.viewedOutfit, std::move(excluded));
				QueuePreview(a_state, ViewedOutfitPieces(a_state));
			}
			BeginPaneButtonRow({ "Edit", "Back" });
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
		// buttons, then Restore Presets (?) at the row's right end, a link as the details' Remove
		// and Restore are, greyed while every preset is as installed. a_escapeTaken: the
		// browser used this frame's Escape, which is otherwise Exit.
		SituationAction DrawSituationButtons(const State& a_state, bool a_escapeTaken)
		{
			constexpr const char* kRestoreHelp =
				"Brings the preset situations back as installed: a removed one returns, empty, and a renamed one "
				"gets its name back. Nobody's outfits change.";

			const auto& request = a_state.request;
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
			const bool restore = Theme::LineHelpLink("Restore Presets", &helpHovered);
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();
			// After EndDisabled, so the tooltip is not drawn faded.
			if (installed && hovered) {
				Theme::Tooltip(kAsInstalled);
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

		// Hands Save Changes' edits to onSituationsSave: the names that changed, every row
		// when any changed, and the outfit whose details show, which stays on
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
				if (name != saved.name) {
					save.edits.push_back({ .situation = saved.situation, .name = std::move(name) });
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
		const auto viewedFrom = keyOf(form.viewedRow);

		std::vector<SituationEntry> entries(request.situations.size());
		form.selected = -1;
		form.viewedRow = -1;
		for (std::size_t i = 0; i < entries.size(); ++i) {
			const auto& info = request.situations[i];
			auto&       entry = entries[i];
			CopyToField(entry.name, info.name);
			entry.row = request.assigned[i].row;
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

	bool HasSituationEdits(const State& a_state)
	{
		return NamesChanged(a_state) || RowsChanged(a_state);
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
	// an outfit's details (ShownPane). Every change is an edit until Save Changes, but Remove
	// and Restore Presets, which write at once after their popups. The preview shows the
	// Inventory page's ticks, or the outfit whose details show.
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
			DrawSituationList(a_state);
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
		ImGui::EndDisabled();
		// While a write runs (Status::kSaving) only Escape, as Back or Exit, gets past
		// BeginDisabled; it does nothing then, as the greyed buttons do nothing.
		if (a_state.status != Status::kEditing) {
			DrawSituationPopups(a_state);
			return;
		}

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
