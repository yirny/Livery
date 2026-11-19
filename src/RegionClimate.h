// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - the heights Survival Mode's areas start at
//   Forms     - the regions its conditions name, the worldspaces it counts as indoors or leaves
//               out, its ice caves; by plugin and local form ID
//   Areas     - the five areas of its region spell
//   Climate   - Weather's Warm, Cool and Freezing region (SituationRules) of a place
//
// The climate of a place as Survival Mode classes it: the conditions on the effects of
// Survival_RegionInfoSpell (ccQDRSSE001-SurvivalMode.esl, 00087F), the order of
// Survival_PlayerLocationInfo.GetCurrentAreaType and its form lists, written out so that no ESL
// is needed. A condition on a region holds in a cell whose region list has it (checked in game
// against the engine's own answer). Nothing here touches the engine: Situations.cpp says which
// of the forms the place is in, and how high.
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace RegionClimate
{
	// --- Constants ------------------------------------------------------------

	// Heights are a position's Z in game units, as the GetPos Z conditions compare them.
	constexpr float kFreezingHeight = 19000.0f;            // anywhere this high is freezing
	constexpr float kFallForestFreezingHeight = 15150.0f;  // Forelhost, Arcwind Point
	constexpr float kSouthForestFreezingHeight = 2000.0f;  // Helgen, Bleak Falls Barrow

	// --- Forms ----------------------------------------------------------------

	struct PluginForm
	{
		std::string_view plugin;
		std::uint32_t    id;  // local form ID
	};

	enum class Region : std::size_t
	{
		kSnow,
		kCoast,
		kCoastFog,
		kMountains,
		kMountainsNoPrecip,
		kTundraMarsh,
		kWinterhold,
		kSkuldafn,
		kTundra,
		kReach,
		kWindhelm,
		kWhiterun,
		kSolitude,
		kMarkarth,
		kBlackreach,
		kPineForest,
		kFallForest,
		kRiften,
		kForelhost,
		kArcwindPoint,
		kFortDawnguard,
		kGrove,
		kVampireCastle,
		kCanyon,
		kIce,
		kPlayground,
		kFalmerValleyBorder,
		kSolstheimSnow,
		kSolstheimMountains,
		kSolstheimSnowHeavy,
		kRavenRock,
		kVolcanicAsh1,
		kVolcanicAsh2,
		kCount
	};

	inline constexpr auto kRegionCount = static_cast<std::size_t>(Region::kCount);

	// In Region's order.
	inline constexpr std::array<PluginForm, kRegionCount> kRegionForms{ {
		{ "Skyrim.esm", 0x0C5853 },      // WeatherSnow
		{ "Skyrim.esm", 0x0C5852 },      // WeatherCoast
		{ "Skyrim.esm", 0x10FE79 },      // WeatherCoastFog
		{ "Skyrim.esm", 0x04D7FD },      // WeatherMountains
		{ "Skyrim.esm", 0x1046CB },      // WeatherMountainsNoPrecip
		{ "Skyrim.esm", 0x0C5855 },      // WeatherTundraMarsh
		{ "Skyrim.esm", 0x10FDC6 },      // WeatherWinterhold
		{ "Skyrim.esm", 0x104AB3 },      // WeatherSkuldafn
		{ "Skyrim.esm", 0x0C5858 },      // WeatherTundra
		{ "Skyrim.esm", 0x0C5856 },      // WeatherReach
		{ "Skyrim.esm", 0x0703C2 },      // WeatherWindhelm
		{ "Skyrim.esm", 0x070408 },      // WeatherWhiterun
		{ "Skyrim.esm", 0x07042A },      // WeatherSolitude
		{ "Skyrim.esm", 0x07042F },      // WeatherMarkarth
		{ "Skyrim.esm", 0x048C2E },      // BlackreachRegion
		{ "Skyrim.esm", 0x02A72D },      // WeatherPineForest
		{ "Skyrim.esm", 0x0C5857 },      // WeatherFallForest
		{ "Skyrim.esm", 0x07042E },      // WeatherRiften
		{ "Update.esm", 0x002EE8 },      // SurvivalForelhost
		{ "Update.esm", 0x002EE7 },      // SurvivalArcwindPoint
		{ "Dawnguard.esm", 0x00D95E },   // AudioExtFallForestDLC1FortDawnguard
		{ "Dawnguard.esm", 0x005099 },   // DLC01WeatherGrove
		{ "Dawnguard.esm", 0x012DAD },   // DLC01VampCastleStorm
		{ "Dawnguard.esm", 0x01A880 },   // DLC01WeatherCanyon
		{ "Dawnguard.esm", 0x0191D3 },   // DLC01WeatherIce
		{ "Dawnguard.esm", 0x0191D2 },   // DLC01WeatherPlayground
		{ "Dawnguard.esm", 0x018C4E },   // DLC1FalmerValleyBorder
		{ "Dragonborn.esm", 0x029F3B },  // WeatherDLC2SolstheimSnow
		{ "Dragonborn.esm", 0x02A201 },  // WeatherDLC2SolstheimMtns
		{ "Dragonborn.esm", 0x02ADC6 },  // WeatherDLC2SolstheimSnowHeavy
		{ "Dragonborn.esm", 0x0374BC },  // WeatherRavenRock01
		{ "Dragonborn.esm", 0x018681 },  // WeatherVolcanicAsh01
		{ "Dragonborn.esm", 0x01EF70 },  // WeatherVolcanicAsh02
	} };

	// Survival_InteriorAreas: worldspaces underground or under a roof, indoors as an interior cell is.
	inline constexpr std::array<PluginForm, 21> kInteriorWorlds{ {
		{ "Skyrim.esm", 0x069857 },     // AlftandWorld
		{ "Skyrim.esm", 0x01EE62 },     // Blackreach
		{ "Skyrim.esm", 0x020DCB },     // BlindCliffCaveWorld
		{ "Skyrim.esm", 0x01FAE2 },     // BloatedMansGrottoWorld
		{ "Skyrim.esm", 0x050015 },     // BrinewaterGrottoWorld
		{ "Skyrim.esm", 0x02C965 },     // DarkwaterWorld
		{ "Skyrim.esm", 0x04F838 },     // EastEmpireWarehouse
		{ "Skyrim.esm", 0x03A9D6 },     // EldergleamSanctuaryWorld
		{ "Skyrim.esm", 0x0243DE },     // FallowstoneCaveWorldStart
		{ "Skyrim.esm", 0x0C97EB },     // FrostmereCryptWorld
		{ "Skyrim.esm", 0x0C350D },     // KatariahWorld
		{ "Skyrim.esm", 0x01CDD3 },     // LabyrinthianWorld
		{ "Skyrim.esm", 0x01CDD9 },     // LabyrinthianWorld03
		{ "Skyrim.esm", 0x021EDB },     // LabyrinthianWorld04
		{ "Skyrim.esm", 0x01E49D },     // MossMotherCavernWorld
		{ "Skyrim.esm", 0x02B101 },     // RedEagleRedoubtWorld
		{ "Skyrim.esm", 0x02A9D8 },     // ShadowgreenCavernWorld
		{ "Skyrim.esm", 0x020BFE },     // SouthfringeWorld
		{ "Dawnguard.esm", 0x0048C7 },  // DLC1AncestorsGladeWorld
		{ "Dawnguard.esm", 0x004BEA },  // DLC1DarkfallPassageWorld
		{ "Dawnguard.esm", 0x002F64 },  // DLC1ForebearsHoldout
	} };

	// Survival_OblivionAreas: planes Survival Mode leaves out of the cold altogether.
	inline constexpr std::array<PluginForm, 5> kOtherworlds{ {
		{ "Skyrim.esm", 0x02EE41 },      // Sovngarde
		{ "Skyrim.esm", 0x034240 },      // BluePalaceWingWorld
		{ "Dawnguard.esm", 0x001408 },   // DLC01SoulCairn
		{ "Dawnguard.esm", 0x00528D },   // DLC01Boneyard
		{ "Dragonborn.esm", 0x01C0B2 },  // DLC2ApocryphaWorld
	} };

	// Survival_ColdInteriorLocations: ice caves, by the location the actor is in (not its parents).
	inline constexpr std::array<PluginForm, 20> kIceCaveLocations{ {
		{ "Skyrim.esm", 0x018EEA },      // BleakcoastCaveLocation
		{ "Skyrim.esm", 0x0FDA46 },      // BonechillPassageLocation
		{ "Skyrim.esm", 0x0BC0A3 },      // ColdRockPassLocation
		{ "Skyrim.esm", 0x019172 },      // DuskglowCreviceLocation
		{ "Skyrim.esm", 0x019186 },      // FrostflowLighthouseLocation
		{ "Skyrim.esm", 0x01918C },      // GreywaterGrottoLocation
		{ "Skyrim.esm", 0x01918F },      // HaemarsShameLocation
		{ "Skyrim.esm", 0x019198 },      // HobsFallCaveLocation
		{ "Skyrim.esm", 0x02D51C },      // SeptimusSignusOutpostLocation
		{ "Skyrim.esm", 0x0192A8 },      // SightlessPitLocation
		{ "Skyrim.esm", 0x01914C },      // SouthfringeSanctumLocation
		{ "Skyrim.esm", 0x0192B5 },      // SteepfallBurrowLocation
		{ "Skyrim.esm", 0x0192B6 },      // StillbornCaveLocation
		{ "Skyrim.esm", 0x0192CA },      // YngvildLocation
		{ "Dawnguard.esm", 0x0162AF },   // DLC1GlacialCreviceLocation
		{ "Dragonborn.esm", 0x014294 },  // DLC2AltarofThrondLocation
		{ "Dragonborn.esm", 0x014298 },  // DLC2BenkongerikeLocation
		{ "Dragonborn.esm", 0x032892 },  // DLC2BristlebackCaveLocation
		{ "Dragonborn.esm", 0x0142A6 },  // DLC2FrosselLocation
		{ "Dragonborn.esm", 0x0142C4 },  // DLC2GlacialCaveLocation
	} };

	// Survival_ColdInteriorCells: ice caves by their cell.
	inline constexpr std::array<PluginForm, 2> kIceCaveCells{ {
		{ "Skyrim.esm", 0x01527E },  // ForsakenCave01
		{ "Skyrim.esm", 0x0152C3 },  // Alftand01
	} };

	// The regions of kRegionForms a place is in, a bit per Region.
	using Regions = std::bitset<kRegionCount>;

	[[nodiscard]] inline bool InAny(const Regions& a_regions, std::initializer_list<Region> a_of)
	{
		for (const auto region : a_of) {
			if (a_regions.test(static_cast<std::size_t>(region))) {
				return true;
			}
		}
		return false;
	}

	// --- Areas ----------------------------------------------------------------

	// The effects of the region spell, in its order; a place may be in several.
	struct Areas
	{
		bool warm = false;
		bool cool = false;
		bool freezing = false;
		bool fallForestFreezing = false;
		bool southForestFreezing = false;
	};

	// Survival Mode's conditions, with one change: Windhelm inside its walls is freezing, as the
	// snow outside its gates is, so walking through a gate does not change the climate.
	[[nodiscard]] inline Areas AreasOf(const Regions& a_regions, float a_height)
	{
		using enum Region;
		const bool solstheimSnow = InAny(a_regions, { kSolstheimSnow, kSolstheimMountains, kSolstheimSnowHeavy });
		Areas      areas;
		areas.warm = !InAny(a_regions, { kSnow }) && !solstheimSnow && a_height < kFreezingHeight &&
		             InAny(a_regions, { kPineForest, kFallForest, kRiften, kFortDawnguard, kRavenRock, kVolcanicAsh1, kVolcanicAsh2 });
		areas.cool = !InAny(a_regions, { kMountains, kSnow }) && InAny(a_regions, { kTundra, kReach, kWhiterun, kSolitude, kMarkarth, kBlackreach, kGrove });
		areas.freezing = a_height >= kFreezingHeight || solstheimSnow ||
		                 InAny(a_regions, { kSnow, kCoast, kCoastFog, kMountains, kMountainsNoPrecip, kTundraMarsh, kWinterhold, kSkuldafn, kWindhelm,
											  kVampireCastle, kCanyon, kIce, kPlayground, kFalmerValleyBorder });
		areas.fallForestFreezing = InAny(a_regions, { kForelhost, kArcwindPoint }) && a_height >= kFallForestFreezingHeight;
		areas.southForestFreezing = InAny(a_regions, { kPineForest, kTundra }) && InAny(a_regions, { kMountains }) &&
		                            a_height >= kSouthForestFreezingHeight;
		return areas;
	}

	// --- Climate --------------------------------------------------------------

	enum class Climate : std::uint8_t
	{
		kNone,  // indoors, or in another plane
		kWarm,
		kCool,
		kFreezing
	};

	// Where the place is, as Survival Mode tells it before its regions.
	enum class Setting : std::uint8_t
	{
		kOutdoors,
		kIndoors,     // an interior cell, or a worldspace of kInteriorWorlds
		kIceCave,     // indoors, in a location of kIceCaveLocations or a cell of kIceCaveCells
		kOtherworld,  // a worldspace of kOtherworlds
	};

	// Indoors none but an ice cave, which is freezing. Outdoors Survival Mode's order: the two
	// mountain areas, then warm, cool, freezing; a place in none of its areas, as a mod's land
	// is, freezing when `a_snowy` (a region of its cell often has snow) and cool otherwise.
	[[nodiscard]] inline Climate ClimateOf(Setting a_setting, const Regions& a_regions, float a_height, bool a_snowy)
	{
		if (a_setting == Setting::kIceCave) {
			return Climate::kFreezing;
		}
		if (a_setting != Setting::kOutdoors) {
			return Climate::kNone;
		}
		const auto areas = AreasOf(a_regions, a_height);
		if (areas.southForestFreezing || areas.fallForestFreezing) {
			return Climate::kFreezing;
		}
		if (areas.warm) {
			return Climate::kWarm;
		}
		if (areas.cool) {
			return Climate::kCool;
		}
		if (areas.freezing) {
			return Climate::kFreezing;
		}
		return a_snowy ? Climate::kFreezing : Climate::kCool;
	}
}
