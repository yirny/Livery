#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "OutfitBrowser.h"

namespace
{
	// Request::categories as Plugin.cpp builds it: classes, situations, custom ones.
	CaptureWindow::Request MakeRequest()
	{
		CaptureWindow::Request request;
		request.categories = {
			{ CaptureWindow::kClassGroup, "Light" },
			{ CaptureWindow::kClassGroup, "Heavy" },
			{ CaptureWindow::kClassGroup, "Clothing" },
			{ CaptureWindow::kClassGroup, "Jewelry" },
			{ "Situation", "Adventuring" },
			{ "Situation", "Town" },
			{ "Situation", "Swimming" },
			{ "Custom Category", "Favourites" },
		};
		request.existingOutfits = {
			{ "iron set", { "Heavy", "Adventuring" }, {} },
			{ "Guard Uniform", { "Light", "Adventuring", "Swimming" }, {} },
			{ "Town Dress", { "Clothing", "Town" }, {} },
			{ "Bare", {}, {} },
			{ "Scout", { "Light", "Adventuring", "Town", "Favourites" }, {} },
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

	void Tick(OutfitBrowser::State& state, const std::string& label)
	{
		for (std::size_t i = 0; i < state.filter.size(); ++i) {
			if (state.filter[i].label == label) {
				state.filterTicked[i] = true;
				return;
			}
		}
		FAIL("no filter row " << label);
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
	CHECK_FALSE(OutfitBrowser::IsSearching(state));
	CHECK_FALSE(OutfitBrowser::IsFiltering(state));
	CHECK(OutfitBrowser::FilterPreview(state) == "Any category");
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
	CHECK(labels == std::vector<std::string>{ "Light", "Heavy", "Clothing", "Jewelry", "No Class", "Adventuring", "Town", "Swimming",
						"No Situation", "Favourites", "No Custom Category" });
}

TEST_CASE("No custom category means no Custom Category rows", "[browser]")
{
	auto request = MakeRequest();
	request.categories.pop_back();
	for (auto& outfit : request.existingOutfits) {
		std::erase(outfit.categories, "Favourites");
	}
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	CHECK(state.filter.back().label == "No Situation");
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

	Tick(state, "Light");
	Tick(state, "Heavy");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Guard Uniform", "iron set", "Scout" });

	Tick(state, "Town");
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
	Tick(state, "No Situation");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare" });

	Tick(state, "Town");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Bare", "Scout", "Town Dress" });
}

TEST_CASE("Search and filter combine", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	Tick(state, "Adventuring");
	Search(state, "s");
	OutfitBrowser::Refresh(state, request);
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "iron set", "Scout" });
}

TEST_CASE("Class sort: the library's class order, then situation, then name", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	state.sort = OutfitBrowser::SortKey::kClass;
	OutfitBrowser::Refresh(state, request);
	// Light: Adventuring + Town (Scout) before Adventuring + Swimming (Guard Uniform).
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "Scout", "Guard Uniform", "iron set", "Town Dress", "Bare" });
}

TEST_CASE("Situation sort: the library's situation order, then class, then name", "[browser]")
{
	const auto           request = MakeRequest();
	OutfitBrowser::State state;
	OutfitBrowser::Reset(state, request);
	state.sort = OutfitBrowser::SortKey::kSituation;
	OutfitBrowser::Refresh(state, request);
	// Adventuring alone, then Adventuring + Town, then Adventuring + Swimming; no situation last.
	CHECK(VisibleNames(state, request) == std::vector<std::string>{ "iron set", "Scout", "Guard Uniform", "Town Dress", "Bare" });
}
