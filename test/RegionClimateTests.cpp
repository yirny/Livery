#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <span>

#include "RegionClimate.h"

namespace
{
	using RegionClimate::Climate;
	using RegionClimate::Region;
	using RegionClimate::Setting;

	RegionClimate::Regions In(std::initializer_list<Region> a_regions)
	{
		RegionClimate::Regions regions;
		for (const auto region : a_regions) {
			regions.set(static_cast<std::size_t>(region));
		}
		return regions;
	}

	Climate Outdoors(std::initializer_list<Region> a_regions, float a_height, bool a_snowy = false)
	{
		return RegionClimate::ClimateOf(Setting::kOutdoors, In(a_regions), a_height, a_snowy);
	}

	bool Unique(std::span<const RegionClimate::PluginForm> a_forms)
	{
		for (std::size_t i = 0; i < a_forms.size(); ++i) {
			for (std::size_t j = i + 1; j < a_forms.size(); ++j) {
				if (a_forms[i].plugin == a_forms[j].plugin && a_forms[i].id == a_forms[j].id) {
					return false;
				}
			}
		}
		return true;
	}
}

// The regions and heights logged in game where Livery's answer matched Survival Mode's.
TEST_CASE("The places checked in game get Survival Mode's climate", "[climate]")
{
	using enum Region;
	CHECK(Outdoors({ kTundra }, -4584) == Climate::kCool);                                     // WhiterunExterior01
	CHECK(Outdoors({ kWhiterun }, -1743) == Climate::kCool);                                   // Whiterun's streets
	CHECK(Outdoors({ kCoastFog, kCoast }, -13844) == Climate::kFreezing);                      // SolitudeLighthouseExterior01
	CHECK(Outdoors({ kCoast }, -13792) == Climate::kFreezing);                                 // DawnstarDBSanctuaryExterior
	CHECK(Outdoors({ kSnow, kTundraMarsh }, -13912) == Climate::kFreezing);                    // MorthalExterior01
	CHECK(Outdoors({ kPineForest }, 8404) == Climate::kWarm);                                  // HelgenExterior
	CHECK(Outdoors({ kMountains, kPineForest }, 6151) == Climate::kFreezing);                  // BleakFallsBarrowExterior
	CHECK(Outdoors({ kFallForest }, 11140) == Climate::kWarm);                                 // RiftenCitySouthGateExterior
	CHECK(Outdoors({ kMountains }, 23488) == Climate::kFreezing);                              // HighHrothgarExterior01
	CHECK(Outdoors({ kSnow }, -12565) == Climate::kFreezing);                                  // WindhelmExterior01
	CHECK(Outdoors({ kFallForest, kForelhost }, 17635) == Climate::kFreezing);                 // ForelhostExterior01
	CHECK(Outdoors({ kMountains, kFallForest, kArcwindPoint }, 18523) == Climate::kFreezing);  // ArcwindPointExterior01
}

TEST_CASE("Windhelm inside its walls is freezing, as outside its gates", "[climate]")
{
	CHECK(Outdoors({ Region::kWindhelm }, -11911) == Climate::kFreezing);
	CHECK(Outdoors({ Region::kWindhelm }, -11911, true) == Climate::kFreezing);
}

TEST_CASE("The mountain areas start at their heights", "[climate]")
{
	using enum Region;
	CHECK(Outdoors({ kPineForest, kMountains }, 1999.0f) == Climate::kWarm);
	CHECK(Outdoors({ kPineForest, kMountains }, 2000.0f) == Climate::kFreezing);
	CHECK(Outdoors({ kTundra, kMountains }, 2000.0f) == Climate::kFreezing);
	CHECK(Outdoors({ kFallForest, kForelhost }, 15149.0f) == Climate::kWarm);
	CHECK(Outdoors({ kFallForest, kForelhost }, 15150.0f) == Climate::kFreezing);
	CHECK(Outdoors({ kFallForest }, 18999.0f) == Climate::kWarm);
	CHECK(Outdoors({ kFallForest }, 19000.0f) == Climate::kFreezing);
}

TEST_CASE("Snow decides only where Survival Mode does not class the place", "[climate]")
{
	using enum Region;
	CHECK(Outdoors({}, 0.0f) == Climate::kCool);
	CHECK(Outdoors({}, 0.0f, true) == Climate::kFreezing);
	CHECK(Outdoors({}, 19000.0f) == Climate::kFreezing);
	CHECK(Outdoors({ kTundra }, 0.0f, true) == Climate::kCool);
	CHECK(Outdoors({ kVolcanicAsh1 }, 0.0f, true) == Climate::kWarm);
}

TEST_CASE("Indoors has no climate but an ice cave's", "[climate]")
{
	const auto regions = In({ Region::kPineForest });
	CHECK(RegionClimate::ClimateOf(Setting::kIndoors, regions, 0.0f, true) == Climate::kNone);
	CHECK(RegionClimate::ClimateOf(Setting::kOtherworld, regions, 0.0f, true) == Climate::kNone);
	CHECK(RegionClimate::ClimateOf(Setting::kIceCave, {}, 0.0f, false) == Climate::kFreezing);
}

TEST_CASE("Each form table names a form once", "[climate]")
{
	CHECK(Unique(RegionClimate::kRegionForms));
	CHECK(Unique(RegionClimate::kInteriorWorlds));
	CHECK(Unique(RegionClimate::kOtherworlds));
	CHECK(Unique(RegionClimate::kIceCaveLocations));
	CHECK(Unique(RegionClimate::kIceCaveCells));
}
