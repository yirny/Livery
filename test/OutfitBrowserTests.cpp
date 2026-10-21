#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "OutfitBrowser.h"

namespace
{
	using CaptureWindow::kClassGroup;
	using CaptureWindow::kSituationGroup;

	// Request::categories as Plugin.cpp builds it: the categories the window offers, then
	// Default and the situations, one per row of `assigned`. The outfits also store an older
	// file's categories (Adventuring, Swimming, Town), which the window does not offer: the
	// stored Town is not the situation Town.
	CaptureWindow::Request MakeRequest()
	{
		CaptureWindow::Request request;
		request.categories = {
			{ kClassGroup, "Light" },
			{ kClassGroup, "Heavy" },
			{ kClassGroup, "Clothing" },
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
		// Default, Town, Home, Sleep, Swimming, Warm. A row shows only what it holds: Town's "Guard
		// Uniform" is hidden behind its chosen "Scout", Home's "Bare" is on its list with none
		// chosen, and the random Swimming holds its whole list.
		request.situations = { "Town", "Home", "Sleep", "Swimming", "Warm" };
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
		OutfitBrowser::SetSort(state, request, key);
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

TEST_CASE("Tags: categories, then the rows holding the outfit; unoffered stored categories stay out", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	CHECK(state.tags[4] == std::vector<std::string>{ "Light", "Heavy", "Town" });        // Scout: the situation Town
	CHECK(state.tags[2] == std::vector<std::string>{ "Clothing", "Default", "Sleep" });  // Town Dress
	CHECK(state.tags[1] == std::vector<std::string>{ "Light", "Swimming" });             // Guard Uniform
	CHECK(state.tags[3].empty());                                                        // Bare
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

TEST_CASE("Filter rows follow the groups, each with its No row", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	std::vector<std::string> labels;
	for (const auto& row : state.filter) {
		labels.push_back(row.label);
	}
	CHECK(labels == std::vector<std::string>{ "Light", "Heavy", "Clothing", "No Category", "Default", "Town", "Home", "Sleep", "Swimming", "Warm",
						"No Situation" });
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

	Tick(state, kSituationGroup, "Town");
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
	Tick(state, kClassGroup, "No Category");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare" });

	Tick(state, kClassGroup, "Clothing");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare", "Town Dress" });
}

TEST_CASE("Search and filter combine", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Tick(state, kClassGroup, "Heavy");
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

TEST_CASE("Category sort: the categories' order, then name", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kClass);
	// Light alone before Light + Heavy; no category last; the situations play no part.
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Guard Uniform", "Scout", "iron set", "Town Dress", "Bare" });
	CHECK(VisibleHeadings(state) == std::vector<std::string>{ "Light", "Light, Heavy", "Heavy", "Clothing", "No Category" });
	CHECK(state.headingCount == 5);
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

TEST_CASE("Rebuild keeps the search and sort for changed outfits", "[browser]")
{
	auto                 request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Search(state, "s");
	Tick(state, kClassGroup, "Light");
	Sort(state, request, OutfitBrowser::SortKey::kClass);

	request.existingOutfits[0].name = "Steel Set";  // iron set, renamed and made Light
	request.existingOutfits[0].categories = { "Light", "Adventuring" };
	OutfitBrowser::Rebuild(state, request);
	CHECK(OutfitBrowser::IsSearching(state));
	CHECK_FALSE(OutfitBrowser::IsFiltering(state));
	CHECK(state.sort == OutfitBrowser::SortKey::kClass);
	CHECK(state.tags[0] == std::vector<std::string>{ "Light" });
	// "s" matches Steel Set, Scout and Town Dress: Light, then Light + Heavy, then Clothing.
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Steel Set", "Scout", "Town Dress" });
}

TEST_CASE("Folded headings: toggled one by one, kept by Rebuild, cleared by SetSort and Reset", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Sort(state, request, OutfitBrowser::SortKey::kClass);
	OutfitBrowser::ToggleFolded(state, "Heavy");
	OutfitBrowser::ToggleFolded(state, "Clothing");
	OutfitBrowser::ToggleFolded(state, "Clothing");
	CHECK(OutfitBrowser::IsFolded(state, "Heavy"));
	CHECK_FALSE(OutfitBrowser::IsFolded(state, "Clothing"));
	CHECK_FALSE(OutfitBrowser::IsFolded(state, "Light, Heavy"));

	OutfitBrowser::Rebuild(state, request);
	CHECK(OutfitBrowser::IsFolded(state, "Heavy"));
	// The list skips folded outfits as it draws them; the browser still lists every one.
	CHECK(state.visible.size() == request.existingOutfits.size());

	Sort(state, request, OutfitBrowser::SortKey::kGender);
	CHECK(state.folded.empty());
	OutfitBrowser::ToggleFolded(state, "Male");
	OutfitBrowser::Reset(state, request);
	CHECK(state.folded.empty());
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
