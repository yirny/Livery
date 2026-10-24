// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CaptureWindow.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Search, filter and sort of the library outfits listed on the outfit pages, and each outfit's
// categories (see OutfitBrowser.cpp).
// Pure logic with no ImGui or SKSE, so the unit tests build it.
//
// Contract: a State belongs to one Request: call Reset with it before anything
// else, and pass that same Request to every later call.
namespace OutfitBrowser
{
	constexpr std::size_t kSearchCapacity = 64;  // bytes, UTF-8, including the terminator

	// The outfit lists' columns, in their order (the header's).
	enum class SortKey
	{
		kIcon,  // OutfitIcons' key order, then its colour order, then the name
		kName   // the name, ASCII case ignored
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
		bool                              descending = false;  // the whole order reversed, ties included
		std::vector<int>                  visible;             // indices into Request::existingOutfits that pass, in display order
		std::vector<std::vector<int>>     categories;          // per outfit: indices into Request::categories it is in, ascending
		// Per outfit: the tooltip of its row's situation mark, "For Town, Warm" from the situations
		// (never Default) the actor wears it for; "" for none.
		std::vector<std::string> situations;
	};

	// Everything listed, by name ascending, nothing typed or ticked.
	void Reset(State& state, const CaptureWindow::Request& request);

	// Rebuilds `visible`. Call after changing search or filterTicked.
	void Refresh(State& state, const CaptureWindow::Request& request);

	// For a request whose outfits or categories changed (CaptureWindow::SetOutfits): keeps
	// the search and the sort, clears the filter's ticks, then refreshes.
	void Rebuild(State& state, const CaptureWindow::Request& request);

	// Sorts by `key`, reversed while `descending`, then refreshes.
	void SetSort(State& state, const CaptureWindow::Request& request, SortKey key, bool descending);

	bool IsSearching(const State& state);
	bool IsFiltering(const State& state);
	void ClearSearch(State& state);
	void ClearFilter(State& state);

	// The ticked rows' labels joined for the dropdown's closed frame; "Any category" for none.
	std::string FilterPreview(const State& state);
}
