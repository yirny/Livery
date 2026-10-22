// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - the gender sort's headings
//   Helpers   - name matching and ordering, the categories an outfit is in, filter rows,
//               group keys and headings
//   Browser   - Reset / Rebuild / Refresh / SetSort, the folded headings, and the
//               search and filter accessors
#include "OutfitBrowser.h"

#include <algorithm>
#include <compare>
#include <string_view>

namespace
{
	// --- Constants ------------------------------------------------------------

	// The gender sort's headings, in its order (GenderRank).
	constexpr std::array<const char*, 3> kGenderHeadings{ "Female", "Male", "Unisex" };

	// --- Helpers --------------------------------------------------------------

	// Outfit names are UTF-8; only ASCII letters fold, as typed text is ASCII only.
	unsigned char AsciiLower(unsigned char a_c)
	{
		return a_c >= 'A' && a_c <= 'Z' ? static_cast<unsigned char>(a_c - 'A' + 'a') : a_c;
	}

	// Every name contains the empty text; ranges::search finds it as an empty match.
	bool ContainsIgnoringCase(std::string_view a_text, std::string_view a_part)
	{
		const auto equal = [](unsigned char a, unsigned char b) { return AsciiLower(a) == AsciiLower(b); };
		return a_part.empty() || !std::ranges::search(a_text, a_part, equal).empty();
	}

	std::weak_ordering CompareNames(std::string_view a_left, std::string_view a_right)
	{
		const auto compare = [](unsigned char a, unsigned char b) { return AsciiLower(a) <=> AsciiLower(b); };
		return std::lexicographical_compare_three_way(a_left.begin(), a_left.end(), a_right.begin(), a_right.end(), compare);
	}

	// The gender sort's place for the library's gender (-1 unisex, 0 male, 1 female): an
	// index into kGenderHeadings.
	std::size_t GenderRank(int a_gender)
	{
		return a_gender == 1 ? 0 : (a_gender == 0 ? 1 : 2);
	}

	// The group a sort key lists the outfits by; "" for kName and kGender.
	std::string_view GroupOf(OutfitBrowser::SortKey a_key)
	{
		switch (a_key) {
		case OutfitBrowser::SortKey::kSituation:
			return CaptureWindow::kSituationGroup;
		case OutfitBrowser::SortKey::kName:
		case OutfitBrowser::SortKey::kGender:
			break;
		}
		return {};
	}

	// Contract: the situation group's n-th category is row n of Request::assigned
	// (Request::categories). A row shows the outfits it holds (OutfitRows::Holds).
	bool HoldsRow(const CaptureWindow::Request& a_request, std::size_t a_row, std::uint32_t a_outfit)
	{
		const auto& assigned = a_request.assigned;
		return a_row < assigned.size() && OutfitRows::Holds(assigned[a_row].row, a_outfit);
	}

	// Indices into Request::categories the outfit is in, ascending: the actor's rows holding it.
	std::vector<int> CategoriesOf(const CaptureWindow::ExistingOutfit& a_outfit, const CaptureWindow::Request& a_request)
	{
		const auto&      options = a_request.categories;
		std::vector<int> in;
		std::size_t      row = 0;
		for (std::size_t i = 0; i < options.size(); ++i) {
			if (options[i].group == CaptureWindow::kSituationGroup && HoldsRow(a_request, row++, a_outfit.id)) {
				in.push_back(static_cast<int>(i));
			}
		}
		return in;
	}

	// "For Town, Warm": the situations (rows after Default, in their order) the actor wears
	// the outfit for; "" for none.
	std::string SituationsText(const CaptureWindow::ExistingOutfit& a_outfit, const CaptureWindow::Request& a_request)
	{
		std::string text;
		for (std::size_t row = 1; row < a_request.assigned.size() && row < a_request.situations.size(); ++row) {
			if (HoldsRow(a_request, row, a_outfit.id)) {
				text += (text.empty() ? "For " : ", ") + a_request.situations[row].name;
			}
		}
		return text;
	}

	// The outfit's categories in one group: indices into Request::categories, ascending.
	std::vector<int> GroupKey(const std::vector<int>& a_categories, const CaptureWindow::Request& a_request, std::string_view a_group)
	{
		std::vector<int> key;
		for (const int index : a_categories) {
			if (a_request.categories[static_cast<std::size_t>(index)].group == a_group) {
				key.push_back(index);
			}
		}
		return key;
	}

	// In Request::categories' order, so Default before Town, and Default + Town before
	// Default + Sleep; an outfit in none of the group's categories goes last.
	std::weak_ordering CompareGroupKeys(const std::vector<int>& a_left, const std::vector<int>& a_right)
	{
		if (a_left.empty() != a_right.empty()) {
			return a_left.empty() ? std::weak_ordering::greater : std::weak_ordering::less;
		}
		return std::lexicographical_compare_three_way(a_left.begin(), a_left.end(), a_right.begin(), a_right.end());
	}

	// A group sort's heading: the outfit's categories in the group joined with ", ", or
	// "No <group>".
	std::string GroupHeading(const std::vector<int>& a_key, const CaptureWindow::Request& a_request, std::string_view a_group)
	{
		if (a_key.empty()) {
			return "No " + std::string{ a_group };
		}
		std::string heading;
		for (const int index : a_key) {
			heading += heading.empty() ? "" : ", ";
			heading += a_request.categories[static_cast<std::size_t>(index)].name;
		}
		return heading;
	}

	std::size_t CountHeadings(const std::vector<std::string>& a_headings)
	{
		std::vector<std::string_view> distinct(a_headings.begin(), a_headings.end());
		std::erase(distinct, std::string_view{});
		std::ranges::sort(distinct);
		distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
		return distinct.size();
	}

	bool MatchesRow(const OutfitBrowser::FilterOption& a_row, const std::vector<int>& a_categories, const CaptureWindow::Request& a_request)
	{
		if (a_row.category >= 0) {
			return std::ranges::binary_search(a_categories, a_row.category);
		}
		return GroupKey(a_categories, a_request, a_row.group).empty();
	}

	// Within a group the ticked rows are alternatives (any one matches); every
	// group with a ticked row must match.
	bool PassesFilter(const OutfitBrowser::State& a_state, const CaptureWindow::Request& a_request, std::size_t a_outfit)
	{
		const auto& rows = a_state.filter;
		const auto& categories = a_state.categories[a_outfit];
		for (std::size_t begin = 0, end = 0; begin < rows.size(); begin = end) {
			bool ticked = false;
			bool matched = false;
			for (end = begin; end < rows.size() && rows[end].group == rows[begin].group; ++end) {
				if (a_state.filterTicked[end]) {
					ticked = true;
					matched = matched || MatchesRow(rows[end], categories, a_request);
				}
			}
			if (ticked && !matched) {
				return false;
			}
		}
		return true;
	}

	// One run of rows per run of Request::categories sharing a group, so a group
	// with no category gets no rows, not even its "No" row.
	std::vector<OutfitBrowser::FilterOption> FilterRows(const CaptureWindow::Request& a_request)
	{
		const auto&                              options = a_request.categories;
		std::vector<OutfitBrowser::FilterOption> rows;
		for (std::size_t begin = 0, end = 0; begin < options.size(); begin = end) {
			const auto& group = options[begin].group;
			for (end = begin; end < options.size() && options[end].group == group; ++end) {
				rows.push_back({ group, options[end].name, static_cast<int>(end) });
			}
			rows.push_back({ group, "No " + group, -1 });
		}
		return rows;
	}
}

namespace OutfitBrowser
{
	// --- Browser --------------------------------------------------------------

	void Reset(State& a_state, const CaptureWindow::Request& a_request)
	{
		a_state.search.fill('\0');
		a_state.sort = SortKey::kName;
		a_state.folded.clear();
		Rebuild(a_state, a_request);
	}

	void Rebuild(State& a_state, const CaptureWindow::Request& a_request)
	{
		a_state.filter = FilterRows(a_request);
		a_state.filterTicked = std::make_unique<bool[]>(a_state.filter.size());  // all false

		a_state.categories.clear();
		a_state.situations.clear();
		for (const auto& outfit : a_request.existingOutfits) {
			a_state.categories.push_back(CategoriesOf(outfit, a_request));
			a_state.situations.push_back(SituationsText(outfit, a_request));
		}
		Refresh(a_state, a_request);
	}

	void Refresh(State& a_state, const CaptureWindow::Request& a_request)
	{
		const auto&            outfits = a_request.existingOutfits;
		const std::string_view search = a_state.search.data();
		a_state.visible.clear();
		for (std::size_t i = 0; i < outfits.size(); ++i) {
			if (ContainsIgnoringCase(outfits[i].name, search) && PassesFilter(a_state, a_request, i)) {
				a_state.visible.push_back(static_cast<int>(i));
			}
		}

		// Every outfit's heading, the hidden ones' too, so headingCount does not follow the search.
		const auto                    group = GroupOf(a_state.sort);
		std::vector<std::vector<int>> keys(outfits.size());
		a_state.headings.assign(outfits.size(), std::string{});
		for (std::size_t i = 0; i < outfits.size(); ++i) {
			if (a_state.sort == SortKey::kGender) {
				a_state.headings[i] = kGenderHeadings[GenderRank(outfits[i].gender)];
			} else if (!group.empty()) {
				keys[i] = GroupKey(a_state.categories[i], a_request, group);
				a_state.headings[i] = GroupHeading(keys[i], a_request, group);
			}
		}
		a_state.headingCount = CountHeadings(a_state.headings);

		const auto before = [&](int a, int b) {
			const auto left = static_cast<std::size_t>(a);
			const auto right = static_cast<std::size_t>(b);
			auto       order = std::weak_ordering::equivalent;
			if (a_state.sort == SortKey::kGender) {
				order = GenderRank(outfits[left].gender) <=> GenderRank(outfits[right].gender);
			} else if (!group.empty()) {
				order = CompareGroupKeys(keys[left], keys[right]);
			}
			if (order == 0) {
				order = CompareNames(outfits[left].name, outfits[right].name);
			}
			return order == 0 ? a < b : order < 0;
		};
		std::ranges::sort(a_state.visible, before);
	}

	void SetSort(State& a_state, const CaptureWindow::Request& a_request, SortKey a_key)
	{
		a_state.sort = a_key;
		a_state.folded.clear();
		Refresh(a_state, a_request);
	}

	bool IsFolded(const State& a_state, const std::string& a_heading)
	{
		return std::ranges::find(a_state.folded, a_heading) != a_state.folded.end();
	}

	void ToggleFolded(State& a_state, const std::string& a_heading)
	{
		if (const auto found = std::ranges::find(a_state.folded, a_heading); found != a_state.folded.end()) {
			a_state.folded.erase(found);
		} else {
			a_state.folded.push_back(a_heading);
		}
	}

	bool IsSearching(const State& a_state)
	{
		return a_state.search[0] != '\0';
	}

	bool IsFiltering(const State& a_state)
	{
		return std::any_of(a_state.filterTicked.get(), a_state.filterTicked.get() + a_state.filter.size(), [](bool a) { return a; });
	}

	void ClearSearch(State& a_state)
	{
		a_state.search.fill('\0');
	}

	void ClearFilter(State& a_state)
	{
		std::fill_n(a_state.filterTicked.get(), a_state.filter.size(), false);
	}

	std::string FilterPreview(const State& a_state)
	{
		std::string preview;
		for (std::size_t i = 0; i < a_state.filter.size(); ++i) {
			if (a_state.filterTicked[i]) {
				preview += preview.empty() ? "" : ", ";
				preview += a_state.filter[i].label;
			}
		}
		return preview.empty() ? "Any category" : preview;
	}
}
