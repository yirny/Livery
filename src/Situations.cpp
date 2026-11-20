// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - the poll interval and redress hold, the water reach and leave, the snowy
//               percent, the location depth
//   Climate   - RegionClimate's climate of the place the player stands in, and the snowy
//               regions
//   Detection - the rows of every situation's rules (SituationRules): sleep, crafting, location
//               types, weather, places, spells and the rest, in water with its hysteresis, and
//               the player's beast form
//   Places   - what the Add place popup offers: the actor's location and its parents, their
//               cell; what the never-worn mark knows of a place (PlaceFacts)
//   Spells    - what the Add spell popup offers: the spells the actor casts on themselves and
//               those behind the visible effects on them, or every spell, potion, scroll and
//               ingredient of the loaded plugins with a visible effect; a spell's visible effects
//   Poll      - a thread queues one SKSE task per interval; the task evaluates every
//               assigned actor with situation outfits, the player included, and
//               redresses at most one
//   API       - Evaluate / RowsHold / PlacesAround / PlaceFacts / SpellsAround / AllSpells /
//               VisibleEffectsOf / Start / Stop
//
// The rules the presets are installed with (SituationTable::PresetRules) follow Tailor 3.0's
// src/events/SituationHandler.cpp and its policy headers (SwimmingPolicy, SleepStatePolicy,
// WarmPolicy, WarmWeather, LocationSituationPolicy).
#include "PCH.h"

#include "Situations.h"

#include "Assignments.h"
#include "CaptureWindow.h"
#include "OutfitPreview.h"
#include "OutfitRows.h"
#include "PieceIcons.h"
#include "RegionClimate.h"
#include "SituationRules.h"
#include "SituationStore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <compare>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
	// --- Constants ------------------------------------------------------------

	constexpr auto kPollInterval = std::chrono::seconds(1);
	// The poll redresses an actor at most once in this long, so a situation that flips back
	// and forth (a shore, a combat edge) does not redress them every poll.
	constexpr auto kRedressHold = std::chrono::seconds(3);
	// Tailor's rule: an actor is in water while the engine reports a water height for them
	// that is no lower than this under their feet, so wading counts.
	constexpr float kWaterReach = 40.0f;  // game units
	// Once they were told in water, they stay in it until the water is this far under their
	// feet, so walking along a shore does not flip a situation in water on and off.
	constexpr float kWaterLeave = 60.0f;  // game units
	// A snowy region: snow weathers hold at least this percent of its weather chance (Windhelm's
	// city region has 40). Read only for a place Survival Mode does not class (RegionClimate).
	constexpr std::uint64_t kSnowyPercent = 40;
	// The most parents a Place row walks up from a location; a mod's loop of parents ends there.
	constexpr int kMaxLocationDepth = 32;

	// --- Climate --------------------------------------------------------------
	//
	// Weather reads around the player whoever wears the situation, as the sky is theirs: the
	// actors the poll dresses are loaded, so near them. Contract: game thread, all.

	// Worked out once per region: its weather list does not change while the game runs.
	bool IsSnowyRegion(RE::TESRegion& a_region)
	{
		static std::unordered_map<RE::FormID, bool> snowy;

		const auto id = a_region.GetFormID();
		if (const auto known = snowy.find(id); known != snowy.end()) {
			return known->second;
		}
		std::uint64_t snow = 0;
		std::uint64_t total = 0;
		if (a_region.dataList) {
			for (auto* data : a_region.dataList->regionDataList) {
				if (!data || data->GetType() != RE::TESRegionData::Type::kWeather) {
					continue;
				}
				for (const auto* entry : static_cast<RE::TESRegionDataWeather*>(data)->weatherTypes) {
					if (!entry || !entry->weather) {
						continue;
					}
					total += entry->chance;
					if (entry->weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
						snow += entry->chance;
					}
				}
			}
		}
		const bool result = total > 0 && snow * 100 >= total * kSnowyPercent;
		if (total > 0) {
			logger::info("Weather region {:08X}: snow {} of {}, {}", id, snow, total, result ? "snowy" : "not snowy");
		}
		snowy.emplace(id, result);
		return result;
	}

	// RegionClimate's forms as FormIDs of this game's load order; 0 for one whose plugin is not
	// loaded, which matches nothing.
	template <class T, std::size_t N>
	std::array<RE::FormID, N> FormIDsOf(const std::array<RegionClimate::PluginForm, N>& a_forms)
	{
		std::array<RE::FormID, N> ids{};
		if (auto* data = RE::TESDataHandler::GetSingleton()) {
			for (std::size_t i = 0; i < N; ++i) {
				if (const auto* form = data->LookupForm<T>(a_forms[i].id, a_forms[i].plugin)) {
					ids[i] = form->GetFormID();
				}
			}
		}
		return ids;
	}

	struct ClimateForms
	{
		std::array<RE::FormID, RegionClimate::kRegionCount>             regions = FormIDsOf<RE::TESRegion>(RegionClimate::kRegionForms);
		std::array<RE::FormID, RegionClimate::kInteriorWorlds.size()>   interiorWorlds = FormIDsOf<RE::TESWorldSpace>(RegionClimate::kInteriorWorlds);
		std::array<RE::FormID, RegionClimate::kOtherworlds.size()>      otherworlds = FormIDsOf<RE::TESWorldSpace>(RegionClimate::kOtherworlds);
		std::array<RE::FormID, RegionClimate::kIceCaveLocations.size()> iceCaveLocations = FormIDsOf<RE::BGSLocation>(RegionClimate::kIceCaveLocations);
		std::array<RE::FormID, RegionClimate::kIceCaveCells.size()>     iceCaveCells = FormIDsOf<RE::TESObjectCELL>(RegionClimate::kIceCaveCells);
	};

	// Contract: first called in game, once the plugins are loaded (the poll and the window
	// evaluate only then), and kept until the game closes, as the loaded plugins are.
	const ClimateForms& Forms()
	{
		static const ClimateForms forms;
		return forms;
	}

	bool Listed(std::span<const RE::FormID> a_ids, RE::FormID a_id)
	{
		return a_id != 0 && std::ranges::find(a_ids, a_id) != a_ids.end();
	}

	// What RegionClimate reads of a place, all but its height.
	struct ClimatePlace
	{
		RE::FormID             cell = 0;
		RE::FormID             location = 0;
		RegionClimate::Setting setting = RegionClimate::Setting::kIndoors;
		RegionClimate::Regions regions;
		bool                   snowy = false;  // a region of the cell is snowy (IsSnowyRegion)
	};

	ClimatePlace ReadClimatePlace(RE::Actor& a_player, RE::TESObjectCELL& a_cell, const RE::BGSLocation* a_location)
	{
		using Setting = RegionClimate::Setting;
		const auto&  forms = Forms();
		ClimatePlace place{ .cell = a_cell.GetFormID(), .location = a_location ? a_location->GetFormID() : 0 };
		const auto*  world = a_cell.IsExteriorCell() ? a_player.GetWorldspace() : nullptr;
		const auto   worldID = world ? world->GetFormID() : 0;
		if (Listed(forms.otherworlds, worldID)) {
			place.setting = Setting::kOtherworld;
		} else if (a_cell.IsExteriorCell() && !Listed(forms.interiorWorlds, worldID)) {
			place.setting = Setting::kOutdoors;
		} else if (Listed(forms.iceCaveLocations, place.location) || Listed(forms.iceCaveCells, place.cell)) {
			place.setting = Setting::kIceCave;
		}
		auto* list = place.setting == Setting::kOutdoors ? a_cell.GetRegionList(false) : nullptr;  // not const: the library's const walk does not compile
		if (!list) {
			return place;
		}
		for (auto* region : *list) {
			if (!region) {
				continue;
			}
			if (const auto known = std::ranges::find(forms.regions, region->GetFormID()); known != forms.regions.end()) {
				place.regions.set(static_cast<std::size_t>(known - forms.regions.begin()));
			}
			place.snowy = place.snowy || IsSnowyRegion(*region);
		}
		return place;
	}

	// The climate where the player stands, in `a_location` (their current location). Their place
	// is read again only as their cell or location changes; the height, which moves within a
	// cell, every time.
	RegionClimate::Climate ClimateAround(RE::Actor& a_player, const RE::BGSLocation* a_location)
	{
		static ClimatePlace place;  // Contract: game thread only.

		auto* cell = a_player.GetParentCell();
		if (!cell) {
			return RegionClimate::Climate::kNone;
		}
		if (cell->GetFormID() != place.cell || (a_location ? a_location->GetFormID() : 0) != place.location) {
			place = ReadClimatePlace(a_player, *cell, a_location);
		}
		return RegionClimate::ClimateOf(place.setting, place.regions, a_player.GetPositionZ(), place.snowy);
	}

	// --- Detection ------------------------------------------------------------
	//
	// Contract: game thread, all.

	// The actors last told in water (InWaterNow), for the hysteresis. Contract: game thread
	// only; it changes only as someone goes in or out of water.
	std::vector<RE::FormID> g_inWater;

	// `a_wasIn`: they were last told in water, so they leave it only past kWaterLeave.
	bool IsInWater(RE::Actor& a_actor, bool a_wasIn)
	{
		const auto* state = a_actor.AsActorState();
		if (state && state->IsSwimming()) {
			return true;
		}
		// A rider's feet are in the stirrups, well above the water the horse wades: only
		// swimming counts (Tailor's rule for the player).
		if (a_actor.IsOnMount()) {
			return false;
		}
		// -infinity while no water is relevant to them.
		const float water = a_actor.GetWaterHeight();
		const float reach = a_wasIn ? kWaterLeave : kWaterReach;
		return std::isfinite(water) && water > a_actor.GetPositionZ() - reach;
	}

	// IsInWater with the hysteresis, noting the answer for the next time.
	bool InWaterNow(RE::Actor& a_actor)
	{
		const auto id = a_actor.GetFormID();
		const auto seen = std::ranges::find(g_inWater, id);
		const bool wasIn = seen != g_inWater.end();
		const bool in = IsInWater(a_actor, wasIn);
		if (in && !wasIn) {
			g_inWater.push_back(id);
		} else if (!in && wasIn) {
			g_inWater.erase(seen);
		}
		return in;
	}

	// Going to bed, in it or getting up (the other states cover chairs and mounts), or on
	// furniture one can sleep in.
	bool IsSleeping(RE::Actor& a_actor)
	{
		using State = RE::SIT_SLEEP_STATE;
		if (const auto* state = a_actor.AsActorState()) {
			switch (state->GetSitSleepState()) {
			case State::kWantToSleep:
			case State::kWaitingForSleepAnim:
			case State::kIsSleeping:
			case State::kWantToWake:
				return true;
			default:
				break;
			}
		}
		const auto  furniture = a_actor.GetOccupiedFurniture().get();
		const auto* base = furniture ? furniture->GetBaseObject() : nullptr;
		const auto* bed = base ? base->As<RE::TESFurniture>() : nullptr;
		return bed && bed->furnFlags.all(RE::TESFurniture::ActiveMarker::kCanSleep);
	}

	// Crafting's value (SituationRules) the furniture is for, 0 for none: by its workbench data,
	// and by the keywords the game's own furniture has (Skyrim.esm and the three DLCs), which
	// keep their editor IDs at runtime. A smelter, tanning rack, cooking pot and oven are
	// workbenches of the plain kind, as a forge is, so their keywords are read before the skill;
	// an ore vein and a chopping block are no workbench at all.
	std::uint32_t CraftingOf(const RE::TESFurniture& a_furniture)
	{
		using Bench = RE::TESFurniture::WorkBenchData::BenchType;
		const auto has = [&a_furniture](std::string_view a_keyword) { return a_furniture.HasKeywordString(a_keyword); };
		const auto skill = a_furniture.workBenchData.usesSkill.get();
		switch (a_furniture.workBenchData.benchType.get()) {
		case Bench::kAlchemy:
		case Bench::kAlchemyExperiment:
			return SituationRules::kAlchemy;
		case Bench::kEnchanting:
		case Bench::kEnchantingExperiment:
			return SituationRules::kEnchanting;
		case Bench::kSmithingWeapon:  // the grindstone
		case Bench::kSmithingArmor:   // the workbench
			return SituationRules::kSmithing;
		case Bench::kCreateObject:
			if (has("CraftingSmelter"sv)) {
				return SituationRules::kSmelting;
			}
			if (has("CraftingTanningRack"sv)) {
				return SituationRules::kTanning;
			}
			if (has("CraftingCookpot"sv) || has("BYOHCraftingOven"sv)) {
				return SituationRules::kCooking;
			}
			if (skill == RE::ActorValue::kSmithing) {  // a forge or anvil
				return SituationRules::kSmithing;
			}
			return skill == RE::ActorValue::kEnchanting ? SituationRules::kEnchanting : 0;  // the staff enchanter
		default:
			break;
		}
		if (has("FurnitureWoodChoppingBlock"sv)) {
			return SituationRules::kWoodcutting;
		}
		const bool vein = has("isPickaxeFloor"sv) || has("isPickaxeTable"sv) || has("isPickaxeWall"sv) || has("DLC2IsPickaxeFloor"sv) ||
		                  has("DLC2IsPickaxeTable"sv) || has("DLC2IsPickaxeWall"sv);
		return vein ? SituationRules::kMining : 0;
	}

	// Crafting's value the furniture the actor uses is for (CraftingOf); 0 on none, read before
	// any keyword.
	std::uint32_t CraftingNow(RE::Actor& a_actor)
	{
		const auto  furniture = a_actor.GetOccupiedFurniture().get();
		const auto* base = furniture ? furniture->GetBaseObject() : nullptr;
		const auto* bench = base ? base->As<RE::TESFurniture>() : nullptr;
		return bench ? CraftingOf(*bench) : 0;
	}

	// The weather that decides: while the sky blends from the last weather to the current one,
	// the last one until the new one is halfway in, so nobody takes a cloak off while the snow
	// still falls. nullptr for none.
	const RE::TESWeather* DecidingWeather(const RE::Sky& a_sky)
	{
		const bool lastDecides = a_sky.lastWeather && a_sky.currentWeatherPct < 0.5f;
		return lastDecides ? a_sky.lastWeather : a_sky.currentWeather;
	}

	// The deciding weather has any of `a_flags`.
	template <class... Flags>
	bool WeatherHas(Flags... a_flags)
	{
		const auto* sky = RE::Sky::GetSingleton();
		const auto* weather = sky ? DecidingWeather(*sky) : nullptr;
		return weather && weather->data.flags.any(a_flags...);
	}

	// Location keywords keep their editor IDs at runtime, so they are named rather than
	// numbered. A location's own keywords only: a room of a city is not in it by its parent.

	// A city, town, settlement, dwelling or inn.
	bool IsInhabited(const RE::BGSLocation& a_location)
	{
		const auto has = [&a_location](std::string_view a_keyword) { return a_location.HasKeywordString(a_keyword); };
		return has("LocTypeCity"sv) || has("LocTypeTown"sv) || has("LocTypeSettlement"sv) || has("LocTypeDwelling"sv) || has("LocTypeInn"sv);
	}

	// Location type's values (SituationRules) the location's own keywords give: Town is any
	// house, the player's included, or inhabited; Castle a jarl's palace or keep, every one of
	// which is a dwelling too, so a town as well; Inn an inn, a town too.
	bool HasLocationType(const RE::BGSLocation& a_location, std::uint32_t a_values)
	{
		const auto has = [&a_location](std::string_view a_keyword) { return a_location.HasKeywordString(a_keyword); };
		return ((a_values & SituationRules::kTown) && (has("LocTypePlayerHouse"sv) || has("LocTypeHouse"sv) || IsInhabited(a_location))) ||
		       ((a_values & SituationRules::kCastle) && has("LocTypeCastle"sv)) ||
		       ((a_values & SituationRules::kInn) && has("LocTypeInn"sv)) ||
		       ((a_values & SituationRules::kPlayerHome) && has("LocTypePlayerHouse"sv)) ||
		       ((a_values & SituationRules::kDungeon) && has("LocTypeDungeon"sv));
	}

	// The game clock in minutes after midnight; noon without a calendar.
	std::uint16_t MinuteOfDay()
	{
		const auto* calendar = RE::Calendar::GetSingleton();
		const float hour = calendar ? calendar->GetHour() : 12.0f;
		const auto  minute = static_cast<int>(hour * 60.0f);
		return static_cast<std::uint16_t>(std::clamp(minute, 0, SituationRules::kMinutesPerDay - 1));
	}

	// What one evaluation reads of one actor, each part once, and only when a row asks for it.
	struct Facts
	{
		RE::Actor&                              actor;
		std::optional<const RE::BGSLocation*>   location;
		std::optional<const RE::TESObjectCELL*> cell;
		std::optional<bool>                     inWater;
		std::optional<std::uint32_t>            crafting;

		const RE::BGSLocation* Location()
		{
			if (!location) {
				location = actor.GetCurrentLocation();
			}
			return *location;
		}

		const RE::TESObjectCELL* Cell()
		{
			if (!cell) {
				cell = actor.GetParentCell();
			}
			return *cell;
		}

		bool InWater()
		{
			if (!inWater) {
				inWater = InWaterNow(actor);
			}
			return *inWater;
		}

		std::uint32_t Crafting()
		{
			if (!crafting) {
				crafting = CraftingNow(actor);
			}
			return *crafting;
		}
	};

	// What one evaluation reads: the wearer's facts, and the player's once a row reads the
	// player (SituationRules::Target::kPlayer), whoever wears the situation.
	struct Evaluation
	{
		Facts                wearer;
		std::optional<Facts> player;

		// The facts a row of `a_target` reads; nullptr for the player while there is none.
		Facts* Of(SituationRules::Target a_target)
		{
			if (a_target == SituationRules::Target::kWearer) {
				return &wearer;
			}
			if (!player) {
				auto* playerRef = RE::PlayerCharacter::GetSingleton();
				if (!playerRef) {
					return nullptr;
				}
				player.emplace(Facts{ .actor = *playerRef });
			}
			return &*player;
		}
	};

	// Whether the actor is in one of State's `a_values` (SituationRules).
	bool InState(std::uint32_t a_values, Facts& a_facts)
	{
		auto&       actor = a_facts.actor;
		const auto* state = actor.AsActorState();
		if ((a_values & SituationRules::kInCombat) && actor.IsInCombat()) {
			return true;
		}
		if ((a_values & SituationRules::kWeaponDrawn) && state && state->IsWeaponDrawn()) {  // drawn, or being put away; fists and spells count
			return true;
		}
		const bool swimming = state && state->IsSwimming();
		if ((a_values & SituationRules::kSwimming) && swimming) {
			return true;
		}
		// Wading is in water but not swimming. The in-water record counts swimming, so one who
		// swims back to the shallows wades at once and leaves the water only past kWaterLeave.
		if ((a_values & SituationRules::kWading) && a_facts.InWater() && !swimming) {
			return true;
		}
		return (a_values & SituationRules::kSleeping) && IsSleeping(actor);
	}

	// Whether the actor's location has one of Location type's `a_values` by its keywords
	// (HasLocationType).
	bool InLocationType(std::uint32_t a_values, Facts& a_facts)
	{
		const auto* location = a_facts.Location();
		return location && HasLocationType(*location, a_values);
	}

	// Whether the actor's cell is an interior, with or without a location.
	bool IsIndoors(Facts& a_facts)
	{
		const auto* cell = a_facts.Cell();
		return cell && cell->IsInteriorCell();
	}

	// Weather's values (SituationRules) that hold now around the player (Climate): Rain and Snow
	// by the sky while they are in an exterior cell, a region by the climate where they stand.
	bool HasWeather(std::uint32_t a_values, Facts& a_player)
	{
		using Flag = RE::TESWeather::WeatherDataFlag;
		using Climate = RegionClimate::Climate;
		const auto* cell = a_player.Cell();
		if (cell && cell->IsExteriorCell() &&
			(((a_values & SituationRules::kRain) && WeatherHas(Flag::kRainy)) || ((a_values & SituationRules::kSnow) && WeatherHas(Flag::kSnow)))) {
			return true;
		}
		if ((a_values & (SituationRules::kWarmRegion | SituationRules::kCoolRegion | SituationRules::kFreezingRegion)) == 0) {
			return false;
		}
		switch (ClimateAround(a_player.actor, a_player.Location())) {
		case Climate::kWarm:
			return (a_values & SituationRules::kWarmRegion) != 0;
		case Climate::kCool:
			return (a_values & SituationRules::kCoolRegion) != 0;
		case Climate::kFreezing:
			return (a_values & SituationRules::kFreezingRegion) != 0;
		default:
			return false;
		}
	}

	// Their cell, or their location or a location it is in (parentLoc), is one of `a_places`
	// (SituationRules::Place::formID; 0, a place not loaded, matches nothing).
	bool InPlaces(const std::vector<SituationRules::Place>& a_places, Facts& a_facts)
	{
		const auto listed = [&a_places](RE::FormID a_id) {
			return std::ranges::any_of(a_places, [a_id](const SituationRules::Place& a_place) { return a_place.formID == a_id; });
		};
		if (const auto* cell = a_facts.Cell(); cell && listed(cell->GetFormID())) {
			return true;
		}
		const auto* location = a_facts.Location();
		for (int depth = 0; location && depth < kMaxLocationDepth; ++depth, location = location->parentLoc) {
			if (listed(location->GetFormID())) {
				return true;
			}
		}
		return false;
	}

	// Calls `a_visit` with each effect on the actor that is neither inactive nor dispelled, until
	// it returns true. The engine's own walk (MagicTarget::VisitEffects, the one CommonLibSSE-NG
	// says is right on every runtime), with no allocation.
	// Contract: game thread; `a_visit` must not change the actor's effects.
	template <class Visit>
	void ForEachLiveEffect(RE::Actor& a_actor, Visit&& a_visit)
	{
		struct Visitor : RE::MagicTarget::ForEachActiveEffectVisitor
		{
			explicit Visitor(Visit& a_function) :
				function(a_function) {}

			RE::BSContainer::ForEachResult Accept(RE::ActiveEffect* a_effect) override
			{
				using Flag = RE::ActiveEffect::Flag;
				if (!a_effect || a_effect->flags.any(Flag::kInactive, Flag::kDispelled)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				return function(*a_effect) ? RE::BSContainer::ForEachResult::kStop : RE::BSContainer::ForEachResult::kContinue;
			}

			Visit& function;
		};
		if (auto* target = a_actor.AsMagicTarget()) {
			Visitor visitor{ a_visit };
			target->VisitEffects(visitor);
		}
	}

	// An effect the game's menus show: named, and not hidden (kHideInUI).
	bool IsVisible(const RE::EffectSetting* a_effect)
	{
		if (!a_effect || a_effect->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kHideInUI)) {
			return false;
		}
		const char* name = a_effect->GetFullName();
		return name && *name;
	}

	// The FormIDs of `a_item`'s visible effects, in its order, repeats kept: a view, so the poll
	// allocates nothing.
	auto VisibleEffectIDs(const RE::MagicItem& a_item)
	{
		return a_item.effects | std::views::filter([](const RE::Effect* a_effect) { return a_effect && IsVisible(a_effect->baseEffect); }) |
		       std::views::transform([](const RE::Effect* a_effect) { return a_effect->baseEffect->GetFormID(); });
	}

	// An effect on them comes from an item with the visible effects of one of `a_spells`
	// (SituationRules::FormRef::effects, SameEffects), the effect itself visible: Stoneflesh's
	// scroll counts for Stoneflesh. A spell without visible effects counts by its own FormID, and
	// one not loaded (FormID 0) by nothing.
	bool UnderSpells(const std::vector<SituationRules::Spell>& a_spells, RE::Actor& a_actor)
	{
		bool found = false;
		ForEachLiveEffect(a_actor, [&a_spells, &found](const RE::ActiveEffect& a_effect) {
			const auto* item = a_effect.spell;
			if (!item) {
				return false;
			}
			const bool visible = a_effect.effect && IsVisible(a_effect.effect->baseEffect);
			found = std::ranges::any_of(a_spells, [item, visible](const SituationRules::Spell& a_spell) {
				if (a_spell.effects.empty()) {
					return a_spell.formID != 0 && a_spell.formID == item->GetFormID();
				}
				return visible && SituationRules::SameEffects(a_spell.effects, VisibleEffectIDs(*item));
			});
			return found;
		});
		return found;
	}

	// Whether one row of a situation's rules holds: its kind's test of the wearer or the player
	// (Row::target), inverted by `not`. A row with nothing selected never holds.
	bool RowHolds(const SituationRules::Row& a_row, Evaluation& a_evaluation)
	{
		using Kind = SituationRules::Kind;
		if (SituationRules::NothingSelected(a_row)) {
			return false;
		}
		auto* facts = a_evaluation.Of(a_row.target);
		if (!facts) {
			return a_row.negated;
		}
		bool holds = false;
		switch (a_row.kind) {
		case Kind::kState:
			holds = InState(a_row.values, *facts);
			break;
		case Kind::kLocationType:
			holds = InLocationType(a_row.values, *facts);
			break;
		case Kind::kIndoors:
			holds = IsIndoors(*facts);
			break;
		case Kind::kPlace:
			holds = InPlaces(a_row.places, *facts);
			break;
		case Kind::kWeather:
			{
				auto* player = a_evaluation.Of(SituationRules::Target::kPlayer);  // the sky is theirs (Climate)
				holds = player && HasWeather(a_row.values, *player);
			}
			break;
		case Kind::kTimeOfDay:
			holds = SituationRules::InTimeRange(a_row.from, a_row.to, MinuteOfDay());
			break;
		case Kind::kSpell:
			holds = UnderSpells(a_row.spells, facts->actor);
			break;
		case Kind::kCrafting:
			holds = (a_row.values & facts->Crafting()) != 0;
			break;
		}
		return holds != a_row.negated;
	}

	// A situation applies: in combat only when it is allowed during combat, then while every row
	// holds; never with no rows.
	bool RulesHold(const SituationRules::Rules& a_rules, Evaluation& a_evaluation, bool a_inCombat)
	{
		if (a_rules.rows.empty() || (a_inCombat && !SituationRules::AppliesInCombat(a_rules))) {
			return false;
		}
		return std::ranges::all_of(a_rules.rows, [&a_evaluation](const SituationRules::Row& a_row) { return RowHolds(a_row, a_evaluation); });
	}

	// A werewolf or Vampire Lord, or a mod's transformation: their races are not playable
	// (Tailor's InBeastForm). The transformation takes the gear off and puts it back on.
	bool IsPlayerInBeastForm(RE::Actor& a_actor)
	{
		const auto* race = a_actor.IsPlayerRef() ? a_actor.GetRace() : nullptr;
		return race && !race->data.flags.all(RE::RACE_DATA::Flag::kPlayable);
	}

	// --- Places ---------------------------------------------------------------
	//
	// Contract: game thread, all; read as the window opens, never on the poll.

	bool IsHold(const RE::BGSLocation& a_location)
	{
		return a_location.HasKeywordString("LocTypeHold"sv);
	}

	// A hold, or a location a hold is in (Tamriel in Skyrim.esm): too wide for a place, which
	// would hold in its named places only, never in the open country between them
	// (docs/custom-situations.md §3). The locations holding a hold are worked out once: the
	// game's locations do not change while it runs.
	bool IsHoldOrWider(const RE::BGSLocation& a_location)
	{
		static const std::vector<RE::FormID> wider = [] {
			std::vector<RE::FormID> parents;
			if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
				for (const auto* location : dataHandler->GetFormArray<RE::BGSLocation>()) {
					if (location && location->parentLoc && IsHold(*location)) {
						parents.push_back(location->parentLoc->GetFormID());
					}
				}
			}
			return parents;
		}();
		return IsHold(a_location) || std::ranges::find(wider, a_location.GetFormID()) != wider.end();
	}

	// A location or cell as a Place, named for the reader: its name, else its editor ID (cells
	// keep theirs), else its FormID. nullopt for a form made in game, which no plugin names.
	std::optional<SituationRules::Place> PlaceOf(const RE::TESForm& a_form)
	{
		const auto* file = a_form.IsDynamicForm() ? nullptr : a_form.GetFile(0);
		if (!file) {
			return std::nullopt;
		}
		std::string name = a_form.GetName();
		if (name.empty()) {
			const auto* editorID = a_form.GetFormEditorID();
			name = editorID && *editorID ? editorID : std::format("{:08X}", a_form.GetFormID());
		}
		return SituationRules::Place{ .ref = { std::string{ file->GetFilename() }, a_form.GetLocalFormID(), std::move(name) }, .formID = a_form.GetFormID() };
	}

	// The locations `a_location` is in, nearest first, up to the first that is a hold or wider
	// (IsHoldOrWider), which is left out with all above it.
	std::vector<SituationRules::Place> ParentsOf(const RE::BGSLocation& a_location)
	{
		std::vector<SituationRules::Place> parents;
		const auto*                        parent = a_location.parentLoc;
		for (int depth = 0; parent && depth < kMaxLocationDepth && !IsHoldOrWider(*parent); ++depth, parent = parent->parentLoc) {
			if (auto place = PlaceOf(*parent)) {
				parents.push_back(std::move(*place));
			}
		}
		return parents;
	}

	// The Location type values (SituationRules::kLocationTypes) the location's own keywords give,
	// as bits: below 32.
	std::uint32_t TypeMaskOf(const RE::BGSLocation& a_location)
	{
		std::uint32_t mask = 0;
		for (const auto& value : SituationRules::kLocationTypes) {
			if (HasLocationType(a_location, value.bit)) {
				mask |= value.bit;
			}
		}
		return mask;
	}

	// What the never-worn mark knows of one place, all but the locations within it
	// (PlaceFacts adds them): its location (a cell's) and that one's parents, its own type mask.
	// `a_isLocation` tells PlaceFacts to add those within it.
	SituationRules::PlaceFact FactOf(std::uint32_t a_formID, bool& a_isLocation)
	{
		SituationRules::PlaceFact fact{ .formID = a_formID };
		const auto*               form = RE::TESForm::LookupByID(a_formID);
		const RE::BGSLocation*    location = form ? form->As<RE::BGSLocation>() : nullptr;
		a_isLocation = location != nullptr;
		if (const auto* cell = form && !location ? form->As<RE::TESObjectCELL>() : nullptr) {
			location = cell->GetLocation();
			if (location) {
				fact.within.push_back(location->GetFormID());
			}
		}
		if (!location) {
			return fact;
		}
		fact.typeMasks = 1u << TypeMaskOf(*location);
		const auto* parent = location->parentLoc;
		for (int depth = 0; parent && depth < kMaxLocationDepth; ++depth, parent = parent->parentLoc) {
			fact.within.push_back(parent->GetFormID());
		}
		return fact;
	}

	// --- Spells ---------------------------------------------------------------
	//
	// Contract: game thread, all; read as the window opens, never on the poll.

	// `a_item`'s visible effects (IsVisible): their FormIDs, ascending, each once.
	std::vector<std::uint32_t> VisibleEffectSet(const RE::MagicItem& a_item)
	{
		std::vector<std::uint32_t> effects;
		for (const auto id : VisibleEffectIDs(a_item)) {
			effects.push_back(id);
		}
		std::ranges::sort(effects);
		const auto repeated = std::ranges::unique(effects);
		effects.erase(repeated.begin(), repeated.end());
		return effects;
	}

	// "- Armor - Stone (ArmorFFSelf25)", a line per visible effect in the item's order, each
	// once; the editor ID only when the game keeps one (it does not by itself; a mod such as
	// powerofthree's Tweaks loads them).
	std::string EffectLines(const RE::MagicItem& a_item)
	{
		std::string                lines;
		std::vector<std::uint32_t> listed;
		for (const auto* effect : a_item.effects) {
			const auto* base = effect ? effect->baseEffect : nullptr;
			if (!IsVisible(base) || std::ranges::find(listed, base->GetFormID()) != listed.end()) {
				continue;
			}
			listed.push_back(base->GetFormID());
			const char* editorID = base->GetFormEditorID();
			lines += std::format("{}- {}", lines.empty() ? "" : "\n", base->GetFullName());
			if (editorID && *editorID) {
				lines += std::format(" ({})", editorID);
			}
		}
		return lines;
	}

	// A spell the Add spell popup offers, named for the reader with its visible effects. nullopt
	// for one the player never sees as an effect (without a name or a visible effect), an
	// enchantment (worn, not cast or used), or one made in game, which no plugin names.
	std::optional<SituationRules::SpellChoice> ChoiceOf(const RE::MagicItem& a_item)
	{
		using Type = RE::MagicSystem::SpellType;
		const char* name = a_item.GetName();
		const auto* file = a_item.IsDynamicForm() ? nullptr : a_item.GetFile(0);
		const auto  type = a_item.GetSpellType();
		if (!file || !name || !*name || type == Type::kEnchantment || type == Type::kStaffEnchantment) {
			return std::nullopt;
		}
		auto effects = VisibleEffectSet(a_item);
		if (effects.empty()) {
			return std::nullopt;
		}
		auto icon = PieceIcons::ResolveMagic(a_item);
		return SituationRules::SpellChoice{ .spell = { .ref = { std::string{ file->GetFilename() }, a_item.GetLocalFormID(), name },
												.formID = a_item.GetFormID(),
												.effects = std::move(effects) },
			.effectLines = EffectLines(a_item),
			.icon = std::move(icon.key),
			.iconColor = icon.color };
	}

	void AddChoice(const RE::MagicItem* a_item, std::vector<SituationRules::SpellChoice>& a_choices)
	{
		if (a_item) {
			if (auto choice = ChoiceOf(*a_item)) {
				a_choices.push_back(std::move(*choice));
			}
		}
	}

	// A spell, power or shout's word the actor can cast or use at will that can leave a visible
	// effect on someone: one delivered to themselves, or one for others whose visible effects are
	// all friendly (Healing Hands, Courage, Magelight), so an Active Effects row checking whoever
	// it lands on can use it. Left out: attacks (any hostile or detrimental effect: Flames, Fury),
	// and those whose effect stays on the caster or lands on no living target (a Conjure, a
	// Reanimate, a push). Abilities, diseases and the rest are offered only while one of their
	// effects is on them.
	bool IsOfferedCast(const RE::MagicItem& a_item)
	{
		using Type = RE::MagicSystem::SpellType;
		using Archetype = RE::EffectArchetype;
		constexpr std::array kNoTargetEffect{ Archetype::kSummonCreature, Archetype::kReanimate, Archetype::kStagger,
			Archetype::kTelekinesis, Archetype::kCommandSummoned, Archetype::kBanish, Archetype::kWerewolfFeed,
			Archetype::kSpawnHazard, Archetype::kSpawnScriptedRef, Archetype::kGrabActor };

		const auto type = a_item.GetSpellType();
		if (type != Type::kSpell && type != Type::kPower && type != Type::kLesserPower && type != Type::kVoicePower) {
			return false;
		}
		bool onSelf = false;
		bool friendly = true;
		bool anyVisible = false;
		for (const auto* effect : a_item.effects) {
			const auto* base = effect ? effect->baseEffect : nullptr;
			if (!IsVisible(base)) {
				continue;
			}
			anyVisible = true;
			onSelf = onSelf || base->data.delivery == RE::MagicSystem::Delivery::kSelf;
			friendly = friendly && !base->IsHostile() && !base->IsDetrimental() &&
			           std::ranges::find(kNoTargetEffect, base->GetArchetype()) == kNoTargetEffect.end();
		}
		return onSelf || (anyVisible && friendly);
	}

	// The spells of an NPC record's or a race's spell list that IsOfferedCast takes, a shout by
	// the spells of its words.
	void AddCastSpells(const RE::TESSpellList* a_list, std::vector<SituationRules::SpellChoice>& a_choices)
	{
		const auto* data = a_list ? a_list->actorEffects : nullptr;
		if (!data) {
			return;
		}
		for (std::uint32_t i = 0; data->spells && i < data->numSpells; ++i) {
			if (const auto* spell = data->spells[i]; spell && IsOfferedCast(*spell)) {
				AddChoice(spell, a_choices);
			}
		}
		for (std::uint32_t i = 0; data->shouts && i < data->numShouts; ++i) {
			if (const auto* shout = data->shouts[i]) {
				for (const auto& word : shout->variations) {
					if (word.spell && IsOfferedCast(*word.spell)) {
						AddChoice(word.spell, a_choices);
					}
				}
			}
		}
	}

	template <class T>
	void AddEveryForm(RE::TESDataHandler& a_data, std::vector<SituationRules::SpellChoice>& a_choices)
	{
		for (const auto* item : a_data.GetFormArray<T>()) {
			AddChoice(item, a_choices);
		}
	}

	// --- Poll -----------------------------------------------------------------

	std::atomic<bool> g_running{ false };  // between Start and Stop
	std::atomic<bool> g_queued{ false };   // a poll task waits to run
	std::jthread      g_poller;

	// Contract: game thread only. What each polled actor was last seen in, so a change is
	// acted on once: a dressing that put nothing on is not tried again until their
	// situations change.
	std::unordered_map<RE::FormID, Situations::Applicable> g_lastSeen;
	// Contract: game thread only. When the poll last redressed each actor (kRedressHold).
	std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_lastRedressed;

	bool IsHeld(RE::FormID a_actor, std::chrono::steady_clock::time_point a_now)
	{
		const auto last = g_lastRedressed.find(a_actor);
		return last != g_lastRedressed.end() && a_now - last->second < kRedressHold;
	}

	void Forget(RE::FormID a_actor)
	{
		g_lastSeen.erase(a_actor);
		g_lastRedressed.erase(a_actor);
		std::erase(g_inWater, a_actor);
	}

	// Nothing while the game is paused or loading, or while the capture window has an actor
	// on the preview. At most one actor is redressed per poll, so a change for everyone at
	// once (the weather) is spread out; the others follow at the next polls. An actor the
	// poll redressed less than kRedressHold ago waits too, their change not yet marked seen,
	// so it is evaluated again at a later poll. The player in
	// beast form is skipped: a redress then would put Livery's pieces in the transformation's
	// way. Turned back, they are seen anew and redressed only when the outfit due differs.
	// Contract: game thread (SKSE task).
	void Poll()
	{
		if (!g_running) {
			return;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || CaptureWindow::IsOpen() || OutfitPreview::IsActive()) {
			return;
		}
		const auto actors = Assignments::ActorsWithSituations();
		const auto gone = [&actors](const auto& a_entry) { return std::ranges::find(actors, a_entry.first) == actors.end(); };
		std::erase_if(g_lastSeen, gone);
		std::erase_if(g_lastRedressed, gone);
		// The player keeps their in-water record even with no situation outfit of their own: a
		// row of someone else's situation may read them (SituationRules::Target::kPlayer).
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto  playerID = player ? player->GetFormID() : 0;
		std::erase_if(g_inWater, [&actors, playerID](RE::FormID a_actor) { return a_actor != playerID && std::ranges::find(actors, a_actor) == actors.end(); });
		const auto time = std::chrono::steady_clock::now();
		bool       redressed = false;
		for (const auto id : actors) {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!actor || !actor->Is3DLoaded() || actor->IsDead() || IsPlayerInBeastForm(*actor)) {
				Forget(id);  // seen anew when they load again or turn back
				continue;
			}
			const auto now = Assignments::SituationsNow(*actor);
			if (const auto seen = g_lastSeen.find(id); seen != g_lastSeen.end() && seen->second == now) {
				continue;
			}
			if (redressed || IsHeld(id, time)) {
				continue;  // their turn comes at a later poll
			}
			g_lastSeen[id] = now;
			redressed = Assignments::WearFor(*actor, now);
			if (redressed) {
				g_lastRedressed[id] = time;
			}
		}
	}

	// Contract: the poll thread. Only the two flags and the task queue are touched here.
	void RunPoller(std::stop_token a_stop)
	{
		while (!a_stop.stop_requested()) {
			std::this_thread::sleep_for(kPollInterval);
			if (a_stop.stop_requested() || !g_running || g_queued.exchange(true)) {
				continue;
			}
			SKSE::GetTaskInterface()->AddTask([] {
				g_queued = false;
				Poll();
			});
		}
	}
}

namespace Situations
{
	Applicable Evaluate(RE::Actor& a_actor, const OutfitRows::Rows& a_rows)
	{
		Applicable applicable;
		const auto order = Order();
		const auto wanted = [&a_rows](Situation a_situation) { return OutfitRows::HoldsAny(a_rows[OutfitRows::RowOf(a_situation)]); };
		if (std::ranges::none_of(order, wanted)) {
			return applicable;  // nothing to read: no situation they could be dressed for
		}
		// A fight is fought in the default outfit, unless a situation is allowed during combat.
		const bool inCombat = a_actor.IsInCombat();
		Evaluation evaluation{ .wearer = { .actor = a_actor } };
		for (const auto situation : order | std::views::reverse) {  // the lowest wins
			if (!wanted(situation)) {
				continue;
			}
			const auto* rules = SituationStore::RulesOf(situation);
			if (rules && RulesHold(*rules, evaluation, inCombat)) {
				applicable.Add(situation);
			}
		}
		return applicable;
	}

	std::vector<bool> RowsHold(RE::Actor& a_actor, std::span<const SituationRules::Row> a_rows)
	{
		Evaluation        evaluation{ .wearer = { .actor = a_actor } };
		std::vector<bool> met;
		met.reserve(a_rows.size());
		for (const auto& row : a_rows) {
			met.push_back(RowHolds(row, evaluation));
		}
		return met;
	}

	SituationRules::PlaceChoices PlacesAround(RE::Actor& a_actor)
	{
		SituationRules::PlaceChoices choices;
		const auto*                  location = a_actor.GetCurrentLocation();
		if (location) {
			choices.location = PlaceOf(*location);
			choices.parents = ParentsOf(*location);
			SituationRules::MergeSameNames(choices);
		}
		if (const auto* cell = a_actor.GetParentCell()) {
			choices.cell = PlaceOf(*cell);
		}
		return choices;
	}

	std::vector<SituationRules::PlaceFact> PlaceFacts(std::span<const std::uint32_t> a_places)
	{
		std::vector<SituationRules::PlaceFact> facts;
		std::vector<bool>                      isLocation;
		for (const auto id : a_places) {
			if (id == 0 || std::ranges::find(facts, id, &SituationRules::PlaceFact::formID) != facts.end()) {
				continue;
			}
			bool location = false;
			facts.push_back(FactOf(id, location));
			isLocation.push_back(location);
		}
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler || std::ranges::none_of(isLocation, std::identity{})) {
			return facts;
		}
		// Each loaded location adds its type mask to every listed location it is in.
		for (const auto* location : dataHandler->GetFormArray<RE::BGSLocation>()) {
			const auto*   parent = location ? location->parentLoc : nullptr;
			std::uint32_t mask = 0;
			for (int depth = 0; parent && depth < kMaxLocationDepth; ++depth, parent = parent->parentLoc) {
				for (std::size_t i = 0; i < facts.size(); ++i) {
					if (isLocation[i] && facts[i].formID == parent->GetFormID()) {
						mask = mask != 0 ? mask : 1u << TypeMaskOf(*location);
						facts[i].typeMasks |= mask;
					}
				}
			}
		}
		return facts;
	}

	std::vector<SituationRules::SpellChoice> SpellsAround(RE::Actor& a_actor)
	{
		std::vector<SituationRules::SpellChoice> choices;
		for (const auto* spell : a_actor.GetActorRuntimeData().addedSpells) {
			if (spell && IsOfferedCast(*spell)) {
				AddChoice(spell, choices);
			}
		}
		AddCastSpells(a_actor.GetActorBase(), choices);
		AddCastSpells(a_actor.GetRace(), choices);
		ForEachLiveEffect(a_actor, [&choices](const RE::ActiveEffect& a_effect) {
			if (a_effect.effect && IsVisible(a_effect.effect->baseEffect)) {
				AddChoice(a_effect.spell, choices);
			}
			return false;
		});
		SituationRules::MergeSameSpells(choices);
		return choices;
	}

	// Contract: built at the first call and never changed after, so the span stays valid until the
	// game closes: the loaded plugins' forms do not change while it runs.
	std::span<const SituationRules::SpellChoice> AllSpells()
	{
		static const std::vector<SituationRules::SpellChoice> all = [] {
			std::vector<SituationRules::SpellChoice> choices;
			if (auto* data = RE::TESDataHandler::GetSingleton()) {
				AddEveryForm<RE::SpellItem>(*data, choices);
				AddEveryForm<RE::AlchemyItem>(*data, choices);
				AddEveryForm<RE::ScrollItem>(*data, choices);
				AddEveryForm<RE::IngredientItem>(*data, choices);
			}
			SituationRules::MergeSameSpells(choices);
			return choices;
		}();
		return all;
	}

	std::vector<std::uint32_t> VisibleEffectsOf(std::uint32_t a_formID)
	{
		const auto* item = a_formID != 0 ? RE::TESForm::LookupByID<RE::MagicItem>(a_formID) : nullptr;
		return item ? VisibleEffectSet(*item) : std::vector<std::uint32_t>{};
	}

	void Start()
	{
		g_running = true;
		if (!g_poller.joinable()) {
			g_poller = std::jthread(RunPoller);
		}
	}

	void SeeAnew()
	{
		g_lastSeen.clear();
	}

	void Stop()
	{
		g_running = false;
		g_lastSeen.clear();
		g_lastRedressed.clear();
		g_inWater.clear();
	}
}
