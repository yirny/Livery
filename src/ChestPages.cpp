// SPDX-License-Identifier: GPL-3.0-or-later
//
// The capture window's chest pages (docs/chest-ui.md; see CaptureWindow.cpp for the window,
// OutfitPages.cpp for the Load page they share, CaptureWindowInternal.h for the state).
//
// FILE MAP
//   Chest list   - the chests by name (the Chests page's, the Load page's with
//                  LoadSource::kChests) and a chest's items read only
//   Chests page  - the tab while Request::offersChests: the chest list, a click opening the
//                  editor on that chest; Create New Chest (the New chest popup: a name, then
//                  Create / Cancel, handed to onChestCreate); Exit
//   Chest editor - the chest's name and two columns: left the items someone carries that are
//                  not in the chest, under the Dress page's group headings in its order,
//                  then the items taken out that nobody carries under their own heading, a
//                  click putting an item in; right the chest's items, a click taking one out;
//                  Add from Wardrobe... / Add from Chest... open the Load page, whose Add puts
//                  an outfit's or another chest's items in; Update (onChestUpdate) and Back,
//                  Remove (Remove chest?, onChestRemove) at the row's right end; Back or
//                  Escape with unsaved edits
//                  shows the unsaved-changes alert. Nothing here dresses anyone.
#include "PCH.h"

#include "CaptureWindowInternal.h"

#include "sarrai/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CaptureWindow::Internal
{
	// --- Chest list -----------------------------------------------------------

	const CaptureWindow::ChestInfo& ChestAt(const State& a_state, int a_index)
	{
		return a_state.request.chests[static_cast<std::size_t>(a_index)];
	}

	int IndexOfChest(const State& a_state, std::uint32_t a_id)
	{
		const auto& chests = a_state.request.chests;
		const auto  found = std::ranges::find(chests, a_id, &CaptureWindow::ChestInfo::id);
		return a_id != 0 && found != chests.end() ? static_cast<int>(found - chests.begin()) : -1;
	}

	// Height of a list of `a_rows` outfit rows (one at least) with no gap between them, exactly
	// as tall as its rows, capped as FittedListHeight caps it with `a_reserved` pixels and the
	// count line under it counted in.
	float RowListHeight(std::size_t a_rows, float a_reserved)
	{
		Theme::PushSmallFont();
		const float countLine = ImGui::GetTextLineHeightWithSpacing();
		Theme::PopSmallFont();
		const float rowHeight = Theme::OutfitRowHeight();
		return FittedListHeight(static_cast<float>((std::max)(a_rows, std::size_t{ 1 })) * rowHeight, rowHeight, a_reserved + countLine);
	}

	int DrawChestList(const State& a_state, int a_selected, int a_except, float a_reserved)
	{
		const auto&       chests = a_state.request.chests;
		const std::size_t listed = chests.size() - (a_except >= 0 ? 1 : 0);
		// No gap between rows, so every other row's stripe meets the next; the rows hold the gap
		// themselves (Theme::OutfitRow). Popped before EndList.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
		int clicked = -1;
		if (Theme::BeginList("chests", RowListHeight(listed, a_reserved))) {
			if (listed == 0) {
				Theme::HintRow(a_except >= 0 ? "No other chests." : kNoChests);
			}
			std::size_t drawn = 0;
			for (std::size_t i = 0; i < chests.size(); ++i) {
				const int index = static_cast<int>(i);
				if (index == a_except) {
					continue;
				}
				ImGui::PushID(index);
				if (Theme::OutfitRow(chests[i].name.c_str(), a_selected == index, {}, drawn % 2 == 1, Theme::Grip::kNone, nullptr)) {
					clicked = index;
				}
				ImGui::PopID();
				++drawn;
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
		Theme::CountText(std::format("{} chest(s)", listed).c_str(), false);
		if (clicked >= 0) {
			QueueMenuSound(kFocusSound);
		}
		return clicked;
	}

	void DrawChestDetails(const State& a_state, int a_index, const char* a_hint)
	{
		if (a_index < 0) {
			Theme::LabelText(a_hint);
			return;
		}
		const auto& chest = ChestAt(a_state, a_index);
		Theme::SectionGap();  // as the label beside it opens (Theme::LabelText)
		ImGui::TextUnformatted(chest.name.c_str());
		Theme::AfterFieldGap();
		Theme::CountText(PieceCountText(chest.pieces.size(), chest.unloadedPieces).c_str());
		DrawPieceNames(a_state, chest.pieces);
	}

	// --- Chests page ----------------------------------------------------------

	void FillChestForm(State& a_state, int a_index)
	{
		auto& form = a_state.chest;
		form.chest = a_index;
		form.name.fill('\0');
		form.pieces.clear();
		form.takenOut.clear();
		if (a_index >= 0) {
			const auto& chest = ChestAt(a_state, a_index);
			std::strncpy(form.name.data(), chest.name.c_str(), kNameCapacity - 1);
			form.pieces = chest.pieces;
		}
	}

	bool HasChestEdits(const State& a_state)
	{
		const auto& form = a_state.chest;
		if (form.chest < 0) {
			return false;
		}
		const auto& chest = ChestAt(a_state, form.chest);
		return Trimmed(form.name.data()) != chest.name || form.pieces != chest.pieces;
	}

	// The New chest popup: the name, then Create (Enter with a name typed) / Cancel (Escape).
	// Returns true on Create; the name is State::chest.newName.
	bool DrawCreatePopup(State& a_state)
	{
		if (!BeginModal(kChestCreatePopup)) {
			return false;
		}
		ImGui::TextUnformatted("Name the new chest.");
		const bool enter = DrawNameField(a_state, a_state.chest.newName, nullptr, nullptr, "Chest Name");
		BeginButtonRow({ "Create", "Cancel" });
		const bool typed = !Trimmed(a_state.chest.newName.data()).empty();
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

	// Hands the name to onChestCreate; until the handler ends with SetOutfits or SetError the
	// page is greyed.
	// Contract: the handler writes chests.json first and changes nothing on a refused or
	// failed write (CLAUDE.md §2.1).
	void RequestChestCreate(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onCreate = a_state.request.onChestCreate;
		SKSE::GetTaskInterface()->AddTask([onCreate, name = Trimmed(a_state.chest.newName.data())] { onCreate(name); });
	}

	// A click on a chest opens the editor on it. Create New Chest asks for a name and creates
	// an empty chest; the editor fills it.
	void DrawChestsPage(State& a_state)
	{
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		Theme::LabelText("Select a chest to open");
		const int clicked = DrawChestList(a_state, -1, -1, 0.0f);
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		BeginButtonRow({ "Create New Chest", "Exit" });
		ImGui::BeginDisabled(!a_state.request.onChestCreate);
		const bool create = Theme::Button("Create New Chest");
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool exit = Theme::Button("Exit") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
		EndButtonRow();
		ImGui::EndDisabled();

		if (clicked >= 0) {
			FillChestForm(a_state, clicked);
			GoToPage(a_state, Page::kChestEdit);
		} else if (create) {
			QueueMenuSound(kOkSound);
			a_state.chest.newName.fill('\0');
			a_state.error.clear();
			a_state.focusNameField = true;
			ImGui::OpenPopup(kChestCreatePopup);
		} else if (exit) {
			RequestLeave(a_state, { .exit = true });
		}
		if (DrawCreatePopup(a_state)) {
			RequestChestCreate(a_state);
		}
	}

	// --- Chest editor ---------------------------------------------------------

	// Puts item `a_piece` in the chest; it leaves the taken-out items.
	void PutIn(State& a_state, std::size_t a_piece)
	{
		auto&      form = a_state.chest;
		const auto at = std::ranges::lower_bound(form.pieces, a_piece);
		if (at == form.pieces.end() || *at != a_piece) {
			form.pieces.insert(at, a_piece);
		}
		std::erase(form.takenOut, a_piece);
	}

	// Takes item `a_piece` out; one nobody carries stays listed left, under Taken out of chest,
	// until the page is left.
	void TakeOut(State& a_state, std::size_t a_piece)
	{
		auto& form = a_state.chest;
		std::erase(form.pieces, a_piece);
		if (a_state.request.pieces[a_piece].group != CaptureWindow::PieceGroup::kNotCarried) {
			return;
		}
		const auto at = std::ranges::lower_bound(form.takenOut, a_piece);
		if (at == form.takenOut.end() || *at != a_piece) {
			form.takenOut.insert(at, a_piece);
		}
	}

	void AddToChest(State& a_state, const std::vector<std::size_t>& a_pieces)
	{
		for (const auto piece : a_pieces) {
			PutIn(a_state, piece);
		}
	}

	// Whether the left column lists the piece: someone carries it and it is not in the chest.
	bool IsOutside(const State& a_state, std::size_t a_piece)
	{
		return a_state.request.pieces[a_piece].group != CaptureWindow::PieceGroup::kNotCarried && !std::ranges::binary_search(a_state.chest.pieces, a_piece);
	}

	// The rows and headings the left column lists: the pieces outside the chest under their
	// groups' headings, then the taken-out items under theirs.
	struct OutsideCount
	{
		std::size_t rows = 0;
		std::size_t headings = 0;
	};

	OutsideCount CountOutside(const State& a_state)
	{
		OutsideCount count;
		std::size_t  group = kPieceGroupCount;
		for (const auto i : a_state.pieceOrder) {
			if (!IsOutside(a_state, i)) {
				continue;
			}
			if (GroupIndex(a_state.request.pieces[i].group) != group) {
				group = GroupIndex(a_state.request.pieces[i].group);
				++count.headings;
			}
			++count.rows;
		}
		if (!a_state.chest.takenOut.empty()) {
			++count.headings;
			count.rows += a_state.chest.takenOut.size();
		}
		return count;
	}

	// Both columns' lists are as tall as the taller one's rows (one row at least), capped as
	// FittedListHeight caps it.
	float EditorListHeight(const State& a_state)
	{
		const float rowHeight = Theme::OutfitRowHeight();
		const auto  outside = CountOutside(a_state);
		const float left = static_cast<float>(outside.rows) * rowHeight + static_cast<float>(outside.headings) * Theme::ListHeadingHeight();
		const float right = static_cast<float>(a_state.chest.pieces.size()) * rowHeight;
		return FittedListHeight((std::max)({ left, right, rowHeight }), rowHeight);
	}

	// An item's row: its icon and name, `a_striped` every other row. Returns true when clicked.
	bool DrawItemRow(const State& a_state, std::size_t a_piece, bool a_striped)
	{
		const auto&          piece = a_state.request.pieces[a_piece];
		const Theme::RowIcon icon{ piece.icon, piece.iconColor };
		ImGui::PushID(static_cast<int>(a_piece));
		const bool clicked = Theme::OutfitRow(piece.name.c_str(), false, {}, a_striped, Theme::Grip::kNone, &icon);
		ImGui::PopID();
		return clicked;
	}

	// Left column: the items outside the chest under the Dress page's group headings, in
	// its order (State::pieceOrder), then the taken-out items. Returns the item clicked.
	std::optional<std::size_t> DrawItemsOutside(const State& a_state, float a_height)
	{
		const auto&                pieces = a_state.request.pieces;
		std::optional<std::size_t> clicked;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
		if (Theme::BeginList("outside", a_height)) {
			std::size_t group = kPieceGroupCount;
			std::size_t drawn = 0;
			std::size_t listed = 0;
			for (const auto i : a_state.pieceOrder) {
				if (!IsOutside(a_state, i)) {
					continue;
				}
				if (GroupIndex(pieces[i].group) != group) {
					group = GroupIndex(pieces[i].group);
					Theme::ListHeading(a_state.headings[group].c_str(), GroupIcon(pieces[i].group));
					drawn = 0;
				}
				if (DrawItemRow(a_state, i, drawn % 2 == 1)) {
					clicked = i;
				}
				++drawn;
				++listed;
			}
			if (!a_state.chest.takenOut.empty()) {
				Theme::ListHeading("Taken out of chest", Theme::HeadingIcon::kChest);
				drawn = 0;
				for (const auto i : a_state.chest.takenOut) {
					if (DrawItemRow(a_state, i, drawn % 2 == 1)) {
						clicked = i;
					}
					++drawn;
					++listed;
				}
			}
			if (listed == 0) {
				Theme::HintRow("No items to put in.");
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
		return clicked;
	}

	// Right column: the chest's items in the Dress page's order. Returns the item clicked.
	std::optional<std::size_t> DrawItemsInside(const State& a_state, float a_height)
	{
		const auto&                inside = a_state.chest.pieces;
		std::optional<std::size_t> clicked;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
		if (Theme::BeginList("inside", a_height)) {
			if (inside.empty()) {
				Theme::HintRow("Empty.");
			}
			std::size_t drawn = 0;
			for (const auto i : a_state.pieceOrder) {
				if (!std::ranges::binary_search(inside, i)) {
					continue;
				}
				if (DrawItemRow(a_state, i, drawn % 2 == 1)) {
					clicked = i;
				}
				++drawn;
			}
		}
		ImGui::PopStyleVar();
		Theme::EndList();
		return clicked;
	}

	// Why Update is greyed, or nullptr.
	const char* UpdateChestReason(const State& a_state)
	{
		if (Trimmed(a_state.chest.name.data()).empty()) {
			return kNeedName;
		}
		return HasChestEdits(a_state) ? nullptr : kNoChanges;
	}

	enum class ChestAction
	{
		kNone,
		kUpdate,
		kRemove,
		kBack
	};

	// Update and Back centred, Remove a link at the row's right end, as the Wardrobe page's.
	ChestAction DrawChestButtons(const State& a_state)
	{
		BeginButtonRow({ "Update", "Back" });
		ImGui::BeginDisabled(!a_state.request.onChestUpdate);
		const bool update = ReasonButton("Update", UpdateChestReason(a_state));
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool back = Theme::Button("Back") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
		ImGui::BeginDisabled(!a_state.request.onChestRemove);
		const bool remove = Theme::LineLink("Remove");
		ImGui::EndDisabled();
		if (update) {
			return ChestAction::kUpdate;
		}
		if (remove) {
			return ChestAction::kRemove;
		}
		return back ? ChestAction::kBack : ChestAction::kNone;
	}

	// Hands the edits to onChestUpdate; until the handler ends with SetOutfits or SetError the
	// page is greyed. Contract: as RequestChestCreate's.
	void RequestChestUpdate(State& a_state)
	{
		const auto& form = a_state.chest;
		a_state.status = Status::kSaving;
		a_state.error.clear();
		CaptureWindow::ChestEdit edit{ .chest = ChestAt(a_state, form.chest).id, .name = Trimmed(form.name.data()), .pieces = form.pieces };
		auto                     onUpdate = a_state.request.onChestUpdate;
		SKSE::GetTaskInterface()->AddTask([onUpdate, edit = std::move(edit)] { onUpdate(edit); });
	}

	// Hands the chest to onChestRemove. Contract: as RequestChestCreate's.
	void RequestChestRemove(State& a_state)
	{
		a_state.status = Status::kSaving;
		a_state.error.clear();
		auto onRemove = a_state.request.onChestRemove;
		SKSE::GetTaskInterface()->AddTask([onRemove, chest = ChestAt(a_state, a_state.chest.chest).id] { onRemove(chest); });
	}

	// The unsaved-changes alert's lines: the rename, the items put in and taken out.
	void DrawChestChangeLines(const State& a_state)
	{
		const auto& form = a_state.chest;
		const auto& chest = ChestAt(a_state, form.chest);
		if (const auto name = Trimmed(form.name.data()); name != chest.name) {
			ImGui::BulletText("%s", std::format("Renamed \"{}\".", name).c_str());
		}
		std::vector<std::size_t> putIn;
		std::ranges::set_difference(form.pieces, chest.pieces, std::back_inserter(putIn));
		std::vector<std::size_t> takenOut;
		std::ranges::set_difference(chest.pieces, form.pieces, std::back_inserter(takenOut));
		if (!putIn.empty()) {
			ImGui::BulletText("%s", std::format("{} item(s) put in.", putIn.size()).c_str());
		}
		if (!takenOut.empty()) {
			ImGui::BulletText("%s", std::format("{} item(s) taken out.", takenOut.size()).c_str());
		}
	}

	std::string UnsavedChestText(const State& a_state)
	{
		return a_state.chest.chest >= 0 ? std::format("You have unsaved changes for \"{}\".", ChestAt(a_state, a_state.chest.chest).name) : std::string{};
	}

	// Names the chest, then Remove / Cancel. Returns true on Remove.
	bool DrawChestRemovePopup(const State& a_state)
	{
		if (!BeginModal(kChestRemovePopup)) {
			return false;
		}
		if (a_state.chest.chest < 0) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return false;
		}
		ImGui::TextUnformatted(std::format("Remove the chest \"{}\"?", ChestAt(a_state, a_state.chest.chest).name).c_str());
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

	// The name field, the two columns and their links, then Update / Remove / Back. A click on
	// an item moves it to the other column; nothing is written until Update.
	void DrawChestEditPage(State& a_state)
	{
		constexpr const char* kNoOtherChests = "No other chests.";
		constexpr const char* kChestLink = "Chest...";

		if (std::exchange(a_state.openLeave, false)) {
			ImGui::OpenPopup(kLeavePopup);
		}
		auto& form = a_state.chest;
		ImGui::BeginDisabled(a_state.status != Status::kEditing);
		DrawNameField(a_state, form.name, nullptr, nullptr, "Chest Name");
		std::optional<std::size_t> putIn;
		std::optional<std::size_t> takeOut;
		CountLinks                 links;
		if (BeginColumns("chest")) {
			const float height = EditorListHeight(a_state);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			Theme::LabelText("Items", Theme::HeadingIcon::kBackpack);
			Theme::AfterFieldGap();
			Theme::CountText(std::format("{} item(s)", CountOutside(a_state).rows).c_str());
			putIn = DrawItemsOutside(a_state, height);
			// Together at the column's right end: the first starts the line under the list.
			links.wardrobe = DrawCountLink("Wardrobe...", a_state.request.existingOutfits.empty() ? kNoOutfits : nullptr, Theme::HeadingIcon::kWardrobe,
				Theme::LinkLine::kNext, Theme::LabelLinkWidth(kAddFrom, Theme::HeadingIcon::kChest, kChestLink));
			links.chest = DrawCountLink(kChestLink, a_state.request.chests.size() <= 1 ? kNoOtherChests : nullptr, Theme::HeadingIcon::kChest);
			ImGui::TableNextColumn();
			ResetCellBaseline();
			Theme::LabelText("In Chest", Theme::HeadingIcon::kChest);
			Theme::AfterFieldGap();
			Theme::CountText(PieceCountText(form.pieces.size(), form.chest >= 0 ? ChestAt(a_state, form.chest).unloadedPieces : 0).c_str());
			takeOut = DrawItemsInside(a_state, height);
			EndColumns();
		}
		if (!a_state.error.empty()) {
			DrawError(a_state.error);
		}
		const auto action = DrawChestButtons(a_state);
		EndButtonRow();
		ImGui::EndDisabled();

		if (putIn) {
			QueueMenuSound(kFocusSound);
			PutIn(a_state, *putIn);
		} else if (takeOut) {
			QueueMenuSound(kFocusSound);
			TakeOut(a_state, *takeOut);
		}
		if (links.wardrobe || links.chest) {
			QueueMenuSound(kOkSound);
			a_state.previewedOutfit = -1;
			a_state.previewedChest = -1;
			a_state.loadTarget = LoadTarget::kChest;
			a_state.loadSource = links.chest ? LoadSource::kChests : LoadSource::kOutfits;
			GoToPage(a_state, Page::kLoad);
		}
		switch (action) {
		case ChestAction::kUpdate:
			QueueMenuSound(kOkSound);
			RequestChestUpdate(a_state);
			break;
		case ChestAction::kRemove:
			QueueMenuSound(kOkSound);
			ImGui::OpenPopup(kChestRemovePopup);
			break;
		case ChestAction::kBack:
			if (!HasChestEdits(a_state)) {
				QueueMenuSound(kCancelSound);
			}
			RequestLeave(a_state, { .page = Page::kChests });
			break;
		case ChestAction::kNone:
			break;
		}
		if (DrawChestRemovePopup(a_state)) {
			RequestChestRemove(a_state);
		}
		switch (DrawLeavePopup(a_state, UnsavedChestText(a_state), UpdateChestReason(a_state), DrawChestChangeLines)) {
		case LeaveChoice::kSave:
			RequestChestUpdate(a_state);
			a_state.leaveAfterSave = true;
			break;
		case LeaveChoice::kDiscard:
			FillChestForm(a_state, form.chest);
			Leave(a_state, *std::exchange(a_state.leave, std::nullopt));
			break;
		case LeaveChoice::kBack:
		case LeaveChoice::kNone:
			break;
		}
	}
}
