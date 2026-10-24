#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "OutfitBrowser.h"

namespace
{
	using CaptureWindow::kSituationGroup;
	using Situations::Situation;

	// Request::categories as Plugin.cpp builds it: Default and the situations, one per row of
	// `assigned`.
	CaptureWindow::Request MakeRequest()
	{
		CaptureWindow::Request request;
		request.categories = {
			{ kSituationGroup, "Default" },
			{ kSituationGroup, "Town" },
			{ kSituationGroup, "Home" },
			{ kSituationGroup, "Sleep" },
			{ kSituationGroup, "Swimming" },
			{ kSituationGroup, "Warm" },
		};
		request.existingOutfits = {
			{ .name = "iron set", .id = 1 },
			{ .name = "Guard Uniform", .id = 2 },
			{ .name = "Town Dress", .id = 3 },
			{ .name = "Bare", .id = 4 },
			{ .name = "Scout", .id = 5 },
		};
		// Default, Town, Home, Sleep, Swimming, Warm. A row shows only what it holds: Town's "Guard
		// Uniform" is hidden behind its chosen "Scout", Home's "Bare" is on its list with none
		// chosen, and the random Swimming holds its whole list.
		request.situations = {
			{ .situation = Situation::kDefault, .name = "Default" },
			{ .situation = Situation::kTown, .name = "Town" },
			{ .situation = Situation::kHome, .name = "Home" },
			{ .situation = Situation::kSleep, .name = "Sleep" },
			{ .situation = Situation::kSwimming, .name = "Swimming" },
			{ .situation = Situation::kWarm, .name = "Warm" },
		};
		request.assigned = {
			{ .row = { .outfits = { 3 }, .chosen = 3 } },
			{ .row = { .outfits = { 5, 2 }, .chosen = 5 } },
			{ .row = { .outfits = { 4 } } },
			{ .row = { .outfits = { 3 }, .chosen = 3 } },
			{ .row = { .outfits = { 2 }, .random = true } },
			{},
		};
		return request;
	}

	std::vector<std::string> VisibleNames(const OutfitBrowser::State& state, const CaptureWindow::Request& request)
	{
		std::vector<std::string> names;
		for (const int index : state.visible) {
			names.push_back(request.existingOutfits[static_cast<std::size_t>(index)].name);
		}
		return names;
	}

	// The names of the rows holding outfit `outfit`, in Request::categories' order.
	std::vector<std::string> RowNames(const OutfitBrowser::State& state, const CaptureWindow::Request& request, std::size_t outfit)
	{
		std::vector<std::string> names;
		for (const int index : state.categories[outfit]) {
			names.push_back(request.categories[static_cast<std::size_t>(index)].name);
		}
		return names;
	}

	void Sort(OutfitBrowser::State& state, const CaptureWindow::Request& request, OutfitBrowser::SortKey key, bool descending = false)
	{
		OutfitBrowser::SetSort(state, request, key, descending);
	}

	OutfitIcons::Icon IconOf(std::size_t key, std::size_t color)
	{
		return { std::string{ OutfitIcons::kKeys[key] }, OutfitIcons::kColors[color] };
	}

	void Tick(OutfitBrowser::State& state, const std::string& group, const std::string& label)
	{
		for (std::size_t i = 0; i < state.filter.size(); ++i) {
			if (state.filter[i].group == group && state.filter[i].label == label) {
				state.filterTicked[i] = true;
				return;
			}
		}
		FAIL("no filter row " << group << " / " << label);
	}

	void Search(OutfitBrowser::State& state, const char* text)
	{
		state.search.fill('\0');
		std::strncpy(state.search.data(), text, state.search.size() - 1);
	}
}

TEST_CASE("Reset lists every outfit by name, ignoring case", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare", "Guard Uniform", "iron set", "Scout", "Town Dress" });
	CHECK(state.sort == OutfitBrowser::SortKey::kName);
	CHECK_FALSE(state.descending);
	CHECK_FALSE(OutfitBrowser::IsSearching(state));
	CHECK_FALSE(OutfitBrowser::IsFiltering(state));
	CHECK(OutfitBrowser::FilterPreview(state) == "Any category");
}

TEST_CASE("The rows holding the outfit, in their order", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	CHECK(RowNames(state, request, 2) == std::vector<std::string>{ "Default", "Sleep" });  // Town Dress
	CHECK(RowNames(state, request, 4) == std::vector<std::string>{ "Town" });              // Scout, chosen over Guard Uniform
	CHECK(RowNames(state, request, 1) == std::vector<std::string>{ "Swimming" });          // Guard Uniform: the random row's list
	CHECK(state.categories[3].empty());                                                    // Bare: on Home's list with none chosen
	CHECK(state.categories[0].empty());                                                    // iron set
}

TEST_CASE("Situation marks name the situations, never Default", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	CHECK(state.situations[2] == "For Sleep");     // Town Dress: Default and Sleep
	CHECK(state.situations[4] == "For Town");      // Scout
	CHECK(state.situations[1] == "For Swimming");  // Guard Uniform
	CHECK(state.situations[0].empty());            // iron set
}

TEST_CASE("Filter rows follow the rows, then the No row", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	std::vector<std::string> labels;
	for (const auto& row : state.filter) {
		labels.push_back(row.label);
	}
	CHECK(labels == std::vector<std::string>{ "Default", "Town", "Home", "Sleep", "Swimming", "Warm", "No Situation" });
}

TEST_CASE("Search matches part of the name, ignoring ASCII case", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Search(state, "IRON");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "iron set" });

	Search(state, "o");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Guard Uniform", "iron set", "Scout", "Town Dress" });

	OutfitBrowser::ClearSearch(state);
	OutfitBrowser::Refresh(state, request);
	CHECK(state.visible.size() == request.existingOutfits.size());
}

TEST_CASE("Filter: any ticked row matches", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);

	Tick(state, kSituationGroup, "Town");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Scout" });

	Tick(state, kSituationGroup, "Swimming");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Guard Uniform", "Scout" });
	CHECK(OutfitBrowser::FilterPreview(state) == "Town, Swimming");

	OutfitBrowser::ClearFilter(state);
	OutfitBrowser::Refresh(state, request);
	CHECK_FALSE(OutfitBrowser::IsFiltering(state));
	CHECK(state.visible.size() == request.existingOutfits.size());
}

TEST_CASE("The No row matches outfits on none of the rows", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Tick(state, kSituationGroup, "No Situation");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare", "iron set" });

	Tick(state, kSituationGroup, "Sleep");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare", "iron set", "Town Dress" });
}

TEST_CASE("Search and filter combine", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Tick(state, kSituationGroup, "No Situation");
	Search(state, "s");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "iron set" });
}

TEST_CASE("Name sort descending reverses the order", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kName, true);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Town Dress", "Scout", "iron set", "Guard Uniform", "Bare" });
}

TEST_CASE("Icon sort: the icon's order, then its colour's, then the name", "[browser]")
{
	auto request = MakeRequest();
	request.existingOutfits[0].icon = IconOf(4, 2);                  // iron set: body, purple
	request.existingOutfits[1].icon = IconOf(0, 0);                  // Guard Uniform: helmet, white
	request.existingOutfits[2].icon = IconOf(4, 0);                  // Town Dress: body, white
	request.existingOutfits[3].icon = { "unknown/icon", 0x123456 };  // Bare: an icon not in the table
	// Scout keeps the default: body, white, as Town Dress has.
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kIcon);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Guard Uniform", "Scout", "Town Dress", "iron set", "Bare" });
	Sort(state, request, OutfitBrowser::SortKey::kIcon, true);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare", "iron set", "Town Dress", "Scout", "Guard Uniform" });
}

TEST_CASE("Rebuild keeps the search and sort for changed outfits and rows", "[browser]")
{
	auto                 request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Search(state, "s");
	Tick(state, kSituationGroup, "Town");
	Sort(state, request, OutfitBrowser::SortKey::kName, true);

	request.existingOutfits[0].name = "Steel Set";  // iron set, renamed and chosen for Town in Scout's place
	request.assigned[1].row.chosen = 1;
	OutfitBrowser::Rebuild(state, request);
	CHECK(OutfitBrowser::IsSearching(state));
	CHECK_FALSE(OutfitBrowser::IsFiltering(state));
	CHECK(state.sort == OutfitBrowser::SortKey::kName);
	CHECK(state.descending);
	CHECK(RowNames(state, request, 0) == std::vector<std::string>{ "Town" });
	CHECK(state.categories[4].empty());  // Scout
	// "s" matches Steel Set, Scout and Town Dress, by name descending.
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Town Dress", "Steel Set", "Scout" });
}

TEST_CASE("Outfit colours read and write as #rrggbb", "[icons]")
{
	CHECK(OutfitIcons::ToHex(0xEAEAEA) == "#eaeaea");
	CHECK(OutfitIcons::FromHex("#AEA5e8") == 0xAEA5E8u);
	CHECK_FALSE(OutfitIcons::FromHex("eaeaea").has_value());
	CHECK_FALSE(OutfitIcons::FromHex("#eaeae").has_value());
	CHECK_FALSE(OutfitIcons::FromHex("#eaeaeg").has_value());
	CHECK(OutfitIcons::kKeys[OutfitIcons::KeyOrder(OutfitIcons::kDefaultKey)] == OutfitIcons::kDefaultKey);
}
