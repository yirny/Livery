// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CaptureWindow.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Search, filter and sort of the library outfits listed on the Load Outfit and
// Update Existing pages (see OutfitBrowser.cpp). Pure logic with no ImGui or
// SKSE, so the unit tests build it.
//
// Contract: a State belongs to one Request: call Reset with it before anything
// else, and pass that same Request to every later call.
namespace OutfitBrowser
{
	constexpr std::size_t kSearchCapacity = 64;  // bytes, UTF-8, including the terminator

	enum class SortKey
	{
		kName,       // name only
		kArmorType,  // armor type, then situation, then name
		kSituation   // situation, then armor type, then name
	};

	// One row of the Filter dropdown: a category, or the row for outfits in
	// none of its group's categories.
	struct FilterOption
	{
		std::string group;     // heading the row is listed under
		std::string label;     // shown as is
		int         category;  // index into Request::categories; -1 for the "No <group>" row
	};

	struct State
	{
		std::array<char, kSearchCapacity> search{};      // matched anywhere in the name, ASCII case ignored
		std::vector<FilterOption>         filter;        // per group of Request::categories: its categories, then its "No <group>" row
		std::unique_ptr<bool[]>           filterTicked;  // one per filter; bool[] for the rows' bool&
		SortKey                           sort = SortKey::kName;
		std::vector<int>                  visible;     // indices into Request::existingOutfits that pass, in display order
		std::vector<std::vector<int>>     categories;  // per outfit: indices into Request::categories it is in, ascending
	};

	// Everything listed, by name, nothing typed or ticked.
	void Reset(State& state, const CaptureWindow::Request& request);

	// Rebuilds `visible`. Call after changing search, filterTicked or sort.
	void Refresh(State& state, const CaptureWindow::Request& request);

	bool IsSearching(const State& state);
	bool IsFiltering(const State& state);
	void ClearSearch(State& state);
	void ClearFilter(State& state);

	// The ticked rows' labels joined for the dropdown's closed frame; "Any category" for none.
	std::string FilterPreview(const State& state);
}
