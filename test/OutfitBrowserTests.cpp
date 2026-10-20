#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "OutfitBrowser.h"

namespace
{
	using CaptureWindow::kClassGroup;
	using CaptureWindow::kCustomGroup;
	using CaptureWindow::kSituationGroup;

	// Request::categories as Plugin.cpp builds it: the Armor Classes, the custom categories by
	// name, then Default and the situations, one per row of `assigned`. The custom Town and
	// the situation Town are different categories.
	CaptureWindow::Request MakeRequest()
	{
		CaptureWindow::Request request;
		request.categories = {
			{ kClassGroup, "Light" },
			{ kClassGroup, "Heavy" },
			{ kClassGroup, "Clothing" },
			{ kCustomGroup, "Adventuring" },
			{ kCustomGroup, "Swimming" },
			{ kCustomGroup, "Town" },
			{ kSituationGroup, "Default" },
			{ kSituationGroup, "Town" },
			{ kSituationGroup, "Home" },
			{ kSituationGroup, "Sleep" },
			{ kSituationGroup, "Swimming" },
			{ kSituationGroup, "Warm" },
		};
		request.existingOutfits = {
			{ .name = "iron set", .categories = { "Heavy", "Adventuring" }, .id = 1, .gender = 0 },
			{ .name = "Guard Uniform", .categories = { "Light", "Adventuring", "Swimming" }, .id = 2, .gender = -1 },
			{ .name = "Town Dress", .categories = { "Clothing", "Town" }, .id = 3, .gender = 1 },
			{ .name = "Bare", .categories = {}, .id = 4, .gender = -1 },
			{ .name = "Scout", .categories = { "Light", "Heavy", "Adventuring", "Town" }, .id = 5, .gender = 1 },
		};
		// Default, Town, Home, Sleep, Swimming, Warm. Home holds pieces that are not a saved outfit.
		request.assigned = { { true, 3 }, { true, 5 }, { true, 0 }, { true, 3 }, { true, 2 }, { false, 0 } };
		return request;
	}

	// MakeRequest with no custom category: the classes and the situations only.
	CaptureWindow::Request MakeRequestWithoutCustom()
	{
		auto request = MakeRequest();
		std::erase_if(request.categories, [](const CaptureWindow::CategoryOption& option) { return option.group == kCustomGroup; });
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

	std::vector<std::string> VisibleHeadings(const OutfitBrowser::State& state)
	{
		std::vector<std::string> headings;
		for (const int index : state.visible) {
			headings.push_back(state.headings[static_cast<std::size_t>(index)]);
		}
		return headings;
	}

	void Sort(OutfitBrowser::State& state, const CaptureWindow::Request& request, OutfitBrowser::SortKey key)
	{
		state.sort = key;
		OutfitBrowser::Refresh(state, request);
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
	CHECK(VisibleHeadings(state) == std::vector<std::string>(5));
	CHECK(state.headingCount == 0);
	CHECK_FALSE(OutfitBrowser::IsSearching(state));
	CHECK_FALSE(OutfitBrowser::IsFiltering(state));
	CHECK(OutfitBrowser::FilterPreview(state) == "Any category");
}

TEST_CASE("Tags: Armor Classes, custom categories, then the rows holding the outfit", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	CHECK(state.tags[4] == std::vector<std::string>{ "Light", "Heavy", "Adventuring", "Town", "Town" });  // Scout
	CHECK(state.tags[2] == std::vector<std::string>{ "Clothing", "Town", "Default", "Sleep" });           // Town Dress
	CHECK(state.tags[3].empty());                                                                         // Bare
}

TEST_CASE("Filter rows follow the groups, each with its No row", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	std::vector<std::string> labels;
	for (const auto& row : state.filter) {
		labels.push_back(row.label);
	}
	CHECK(labels == std::vector<std::string>{ "Light", "Heavy", "Clothing", "No Armor Class", "Adventuring", "Swimming", "Town",
						"No Custom Category", "Default", "Town", "Home", "Sleep", "Swimming", "Warm", "No Situation" });
}

TEST_CASE("No custom category: no Custom Category rows or sort", "[browser]")
{
	const auto           request = MakeRequestWithoutCustom();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	CHECK(std::ranges::none_of(state.filter, [](const OutfitBrowser::FilterOption& row) { return row.group == kCustomGroup; }));
	CHECK_FALSE(OutfitBrowser::OffersSort(request, OutfitBrowser::SortKey::kCustom));
	CHECK(OutfitBrowser::OffersSort(request, OutfitBrowser::SortKey::kClass));
	CHECK(OutfitBrowser::OffersSort(request, OutfitBrowser::SortKey::kSituation));
	CHECK(OutfitBrowser::OffersSort(MakeRequest(), OutfitBrowser::SortKey::kCustom));

	// The situations still follow the rows once the custom categories are gone.
	CHECK(state.tags[2] == std::vector<std::string>{ "Clothing", "Default", "Sleep" });  // Town Dress
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

TEST_CASE("Filter: any ticked row within a group, every ticked group", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);

	Tick(state, kClassGroup, "Light");
	Tick(state, kClassGroup, "Heavy");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Guard Uniform", "iron set", "Scout" });

	Tick(state, kCustomGroup, "Town");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Scout" });
	CHECK(OutfitBrowser::FilterPreview(state) == "Light, Heavy, Town");

	OutfitBrowser::ClearFilter(state);
	OutfitBrowser::Refresh(state, request);
	CHECK_FALSE(OutfitBrowser::IsFiltering(state));
	CHECK(state.visible.size() == request.existingOutfits.size());
}

TEST_CASE("A No row matches outfits in none of its group's categories", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Tick(state, kCustomGroup, "No Custom Category");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare" });

	Tick(state, kCustomGroup, "Town");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare", "Scout", "Town Dress" });
}

TEST_CASE("Search and filter combine", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Tick(state, kCustomGroup, "Adventuring");
	Search(state, "s");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "iron set", "Scout" });
}

TEST_CASE("Gender sort: Female, Male, Unisex, then name", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kGender);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Scout", "Town Dress", "iron set", "Bare", "Guard Uniform" });
	CHECK(VisibleHeadings(state) == std::vector<std::string>{ "Female", "Female", "Male", "Unisex", "Unisex" });
	CHECK(state.headingCount == 3);
}

TEST_CASE("Armor Class sort: the classes' order, then name", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kClass);
	// Light alone before Light + Heavy; no class last; other groups play no part.
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Guard Uniform", "Scout", "iron set", "Town Dress", "Bare" });
	CHECK(VisibleHeadings(state) == std::vector<std::string>{ "Light", "Light, Heavy", "Heavy", "Clothing", "No Armor Class" });
	CHECK(state.headingCount == 5);
}

TEST_CASE("Custom Category sort: by name, then the outfit's name", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kCustom);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "iron set", "Guard Uniform", "Scout", "Town Dress", "Bare" });
	CHECK(VisibleHeadings(state) ==
		  std::vector<std::string>{ "Adventuring", "Adventuring, Swimming", "Adventuring, Town", "Town", "No Custom Category" });
}

TEST_CASE("Situation sort: Default, then the situations' order", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kSituation);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Town Dress", "Scout", "Guard Uniform", "Bare", "iron set" });
	CHECK(VisibleHeadings(state) == std::vector<std::string>{ "Default, Sleep", "Town", "Swimming", "No Situation", "No Situation" });
	CHECK(state.headingCount == 4);
}

TEST_CASE("The heading count covers the outfits the search hides", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Search(state, "iron");
	Sort(state, request, OutfitBrowser::SortKey::kClass);
	CHECK(VisibleHeadings(state) == std::vector<std::string>{ "Heavy" });
	CHECK(state.headingCount == 5);
}
