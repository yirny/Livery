// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - the groups the sort keys use
//   Helpers   - name matching and ordering, filter rows, group keys
//   Browser   - Reset / Refresh and the search and filter accessors
#include "OutfitBrowser.h"

#include <algorithm>
#include <compare>
#include <string_view>

namespace
{
	// Contract: the groups Library gives its armor-type and situation
	// categories (CaptureWindow::kArmorTypeGroup and its "Situation").
	constexpr std::string_view kArmorTypeGroup = CaptureWindow::kArmorTypeGroup;
	constexpr std::string_view kSituationGroup = "Situation";

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

	// In the library's category order (Request::categories), so Clothing before Light
	// Armor, and Adventuring + Town before Adventuring + Swimming; an outfit in
	// none of the group's categories goes last.
	std::weak_ordering CompareGroupKeys(const std::vector<int>& a_left, const std::vector<int>& a_right)
	{
		if (a_left.empty() != a_right.empty()) {
			return a_left.empty() ? std::weak_ordering::greater : std::weak_ordering::less;
		}
		return std::lexicographical_compare_three_way(a_left.begin(), a_left.end(), a_right.begin(), a_right.end());
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
	// with no category (no custom ones) gets no rows, not even its "No" row.
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
		a_state.filter = FilterRows(a_request);
		a_state.filterTicked = std::make_unique<bool[]>(a_state.filter.size());  // all false

		const auto& options = a_request.categories;
		a_state.categories.clear();
		for (const auto& outfit : a_request.existingOutfits) {
			auto& in = a_state.categories.emplace_back();
			for (std::size_t i = 0; i < options.size(); ++i) {
				if (std::ranges::find(outfit.categories, options[i].name) != outfit.categories.end()) {
					in.push_back(static_cast<int>(i));
				}
			}
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

		std::vector<std::vector<int>> armorTypes(outfits.size());
		std::vector<std::vector<int>> situations(outfits.size());
		if (a_state.sort != SortKey::kName) {
			for (const int i : a_state.visible) {
				const auto& categories = a_state.categories[static_cast<std::size_t>(i)];
				armorTypes[static_cast<std::size_t>(i)] = GroupKey(categories, a_request, kArmorTypeGroup);
				situations[static_cast<std::size_t>(i)] = GroupKey(categories, a_request, kSituationGroup);
			}
		}
		const auto before = [&](int a, int b) {
			const auto  left = static_cast<std::size_t>(a);
			const auto  right = static_cast<std::size_t>(b);
			const auto& first = a_state.sort == SortKey::kSituation ? situations : armorTypes;
			const auto& second = a_state.sort == SortKey::kSituation ? armorTypes : situations;
			auto        order = std::weak_ordering::equivalent;
			if (a_state.sort != SortKey::kName) {
				order = CompareGroupKeys(first[left], first[right]);
				if (order == 0) {
					order = CompareGroupKeys(second[left], second[right]);
				}
			}
			if (order == 0) {
				order = CompareNames(outfits[left].name, outfits[right].name);
			}
			return order == 0 ? a < b : order < 0;
		};
		std::ranges::sort(a_state.visible, before);
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
