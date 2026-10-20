// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CaptureWindow.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Search, filter and sort of the library outfits listed on the outfit pages, each outfit's
// tags and heading, and the headings folded away (see OutfitBrowser.cpp).
// Pure logic with no ImGui or SKSE, so the unit tests build it.
//
// Contract: a State belongs to one Request: call Reset with it before anything
// else, and pass that same Request to every later call.
namespace OutfitBrowser
{
	constexpr std::size_t kSearchCapacity = 64;  // bytes, UTF-8, including the terminator

	// Every key but kName lists the outfits under headings, by name within each.
	enum class SortKey
	{
		kName,      // name only
		kGender,    // Female, Male, then Unisex
		kClass,     // the outfit's categories (CaptureWindow::kClassGroup)
		kSituation  // the actor's rows holding it (kSituationGroup)
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
		std::array<char, kSearchCapacity>     search{};      // matched anywhere in the name, ASCII case ignored
		std::vector<FilterOption>             filter;        // per group of Request::categories: its categories, then its "No <group>" row
		std::unique_ptr<bool[]>               filterTicked;  // one per filter; bool[] for the rows' bool&
		SortKey                               sort = SortKey::kName;
		std::vector<int>                      visible;     // indices into Request::existingOutfits that pass, in display order
		std::vector<std::vector<int>>         categories;  // per outfit: indices into Request::categories it is in, ascending
		std::vector<std::vector<std::string>> tags;        // per outfit: the names of those categories, in that order, for its row
		// Per outfit: the heading it is listed under for `sort`; "" for every outfit with kName.
		std::vector<std::string> headings;
		// Distinct headings among every outfit's, so the list's height does not follow the search.
		std::size_t headingCount = 0;
		// The headings whose outfits the list folds away; empty after Reset and SetSort.
		std::vector<std::string> folded;
	};

	// Everything listed, by name, nothing typed or ticked.
	void Reset(State& state, const CaptureWindow::Request& request);

	// Rebuilds `visible`, `headings` and `headingCount`. Call after changing search,
	// filterTicked or sort.
	void Refresh(State& state, const CaptureWindow::Request& request);

	// For a request whose outfits or categories changed (CaptureWindow::SetOutfits): keeps
	// the search, the sort and the folded headings, clears the filter's ticks, then refreshes.
	void Rebuild(State& state, const CaptureWindow::Request& request);

	// Sorts by `key` with every heading unfolded, then refreshes.
	void SetSort(State& state, const CaptureWindow::Request& request, SortKey key);

	// Whether the outfits under `heading` are folded away; a click on the heading toggles it.
	bool IsFolded(const State& state, const std::string& heading);
	void ToggleFolded(State& state, const std::string& heading);

	bool IsSearching(const State& state);
	bool IsFiltering(const State& state);
	void ClearSearch(State& state);
	void ClearFilter(State& state);

	// The ticked rows' labels joined for the dropdown's closed frame; "Any category" for none.
	std::string FilterPreview(const State& state);
}
