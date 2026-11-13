// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - the poll interval and redress hold, the water reach and leave, the snow share
//   Detection - the presets' conditions (sleep, cold outdoors, home, castle, town), the
//               rows of the user's situations (SituationRules), in water with its
//               hysteresis, and the player's beast form
//   Poll      - a thread queues one SKSE task per interval; the task evaluates every
//               assigned actor with situation outfits, the player included, and
//               redresses at most one
//   API       - Evaluate / Start / Stop
//
// The presets' rules follow Tailor 3.0's src/events/SituationHandler.cpp and its policy headers
// (SwimmingPolicy, SleepStatePolicy, WarmPolicy, WarmWeather, LocationSituationPolicy).
#include "PCH.h"

#include "Situations.h"

#include "Assignments.h"
#include "CaptureWindow.h"
#include "OutfitPreview.h"
#include "OutfitRows.h"
#include "SituationRules.h"
#include "SituationStore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
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
	// A snowy region: snow weathers hold at least one part in this many of its weather chance.
	constexpr std::uint64_t kSnowShare = 4;

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
		const bool result = total > 0 && snow * kSnowShare >= total;
		logger::info("Weather region {:08X}: snow {} of {}, {}", id, snow, total, result ? "snowy" : "not snowy");
		snowy.emplace(id, result);
		return result;
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

	// The sky's region, the one around the player, is snowy (IsSnowyRegion).
	bool InSnowyRegion()
	{
		auto* sky = RE::Sky::GetSingleton();
		return sky && sky->region && IsSnowyRegion(*sky->region);
	}

	// Outdoors in rain or snow, or anywhere outdoors in a snowy region (whose clear days are
	// its own); a cloudy sky alone is not cold.
	bool IsColdOutdoors(RE::Actor& a_actor)
	{
		using Flag = RE::TESWeather::WeatherDataFlag;
		const auto* cell = a_actor.GetParentCell();
		if (!cell || !cell->IsExteriorCell()) {
			return false;
		}
		return WeatherHas(Flag::kRainy, Flag::kSnow) || InSnowyRegion();
	}

	// Location keywords keep their editor IDs at runtime, so they are named rather than
	// numbered. A location's own keywords only: a room of a city is not in it by its parent.

	// Any house for an NPC, their own for the player.
	bool IsHome(const RE::Actor& a_actor, const RE::BGSLocation& a_location)
	{
		return a_location.HasKeywordString("LocTypePlayerHouse"sv) || (!a_actor.IsPlayerRef() && a_location.HasKeywordString("LocTypeHouse"sv));
	}

	// A jarl's palace or keep. Every one in the game is a dwelling too, so in a town (IsTown).
	bool IsCastle(const RE::BGSLocation& a_location)
	{
		return a_location.HasKeywordString("LocTypeCastle"sv);
	}

	// A city, town, settlement, dwelling or inn.
	bool IsInhabited(const RE::BGSLocation& a_location)
	{
		const auto has = [&a_location](std::string_view a_keyword) { return a_location.HasKeywordString(a_keyword); };
		return has("LocTypeCity"sv) || has("LocTypeTown"sv) || has("LocTypeSettlement"sv) || has("LocTypeDwelling"sv) || has("LocTypeInn"sv);
	}

	// Inhabited, or a home (IsHome), so a home without an outfit of its own falls back to the
	// Town one.
	bool IsTown(const RE::Actor& a_actor, const RE::BGSLocation& a_location)
	{
		return IsHome(a_actor, a_location) || IsInhabited(a_location);
	}

	// `a_location`: their current location, or null outside any.
	bool Holds(Situations::Condition a_condition, RE::Actor& a_actor, const RE::BGSLocation* a_location)
	{
		using Condition = Situations::Condition;
		switch (a_condition) {
		case Condition::kSleeping:
			return IsSleeping(a_actor);
		case Condition::kCold:
			return IsColdOutdoors(a_actor);
		case Condition::kHome:
			return a_location && IsHome(a_actor, *a_location);
		case Condition::kCastle:
			return a_location && IsCastle(*a_location);
		case Condition::kTown:
			return a_location && IsTown(a_actor, *a_location);
		}
		return false;
	}

	// Location type's values (SituationRules) the location has, as the presets read
	// keywords: Town is any house, the player's included, or inhabited.
	bool HasLocationType(const RE::BGSLocation& a_location, std::uint32_t a_values)
	{
		const auto has = [&a_location](std::string_view a_keyword) { return a_location.HasKeywordString(a_keyword); };
		return ((a_values & SituationRules::kTown) && (has("LocTypePlayerHouse"sv) || has("LocTypeHouse"sv) || IsInhabited(a_location))) ||
		       ((a_values & SituationRules::kCastle) && IsCastle(a_location)) ||
		       ((a_values & SituationRules::kPlayerHome) && has("LocTypePlayerHouse"sv)) ||
		       ((a_values & SituationRules::kDungeon) && has("LocTypeDungeon"sv));
	}

	// Weather's values (SituationRules) that hold now; no cell test, so indoors too.
	bool HasWeather(std::uint32_t a_values)
	{
		using Flag = RE::TESWeather::WeatherDataFlag;
		return ((a_values & SituationRules::kRain) && WeatherHas(Flag::kRainy)) ||
		       ((a_values & SituationRules::kSnow) && WeatherHas(Flag::kSnow)) ||
		       ((a_values & SituationRules::kSnowyRegion) && InSnowyRegion());
	}

	// The game clock in minutes after midnight; noon without a calendar.
	std::uint16_t MinuteOfDay()
	{
		const auto* calendar = RE::Calendar::GetSingleton();
		const float hour = calendar ? calendar->GetHour() : 12.0f;
		const auto  minute = static_cast<int>(hour * 60.0f);
		return static_cast<std::uint16_t>(std::clamp(minute, 0, SituationRules::kMinutesPerDay - 1));
	}

	// What one evaluation reads of an actor, each part once, and only when a row asks for it.
	struct Facts
	{
		RE::Actor&                            actor;
		std::optional<const RE::BGSLocation*> location;
		std::optional<bool>                   inWater;

		const RE::BGSLocation* Location()
		{
			if (!location) {
				location = actor.GetCurrentLocation();
			}
			return *location;
		}

		bool InWater()
		{
			if (!inWater) {
				inWater = InWaterNow(actor);
			}
			return *inWater;
		}
	};

	// Whether one row of a user's situation holds: its kind's test, inverted by `not`. A row
	// with values and none ticked never holds.
	bool RowHolds(const SituationRules::Row& a_row, Facts& a_facts)
	{
		using Kind = SituationRules::Kind;
		if (SituationRules::HasValues(a_row.kind) && a_row.values == 0) {
			return false;
		}
		auto&       actor = a_facts.actor;
		const auto* state = actor.AsActorState();
		bool        holds = false;
		switch (a_row.kind) {
		case Kind::kInCombat:
			holds = actor.IsInCombat();
			break;
		case Kind::kSleeping:
			holds = IsSleeping(actor);
			break;
		case Kind::kInWater:
			holds = a_facts.InWater();
			break;
		case Kind::kSwimming:
			holds = state && state->IsSwimming();
			break;
		case Kind::kIndoors:
			{
				const auto* cell = actor.GetParentCell();
				holds = cell && cell->IsInteriorCell();
			}
			break;
		case Kind::kLocationType:
			{
				const auto* location = a_facts.Location();
				holds = location && HasLocationType(*location, a_row.values);
			}
			break;
		case Kind::kWeather:
			holds = HasWeather(a_row.values);
			break;
		case Kind::kTimeOfDay:
			holds = SituationRules::InTimeRange(a_row.from, a_row.to, MinuteOfDay());
			break;
		}
		return holds != a_row.negated;
	}

	// A user's situation applies: in combat only when it applies in combat, then while every
	// row holds; never with no rows.
	bool RulesHold(const SituationRules::Rules& a_rules, Facts& a_facts, bool a_inCombat)
	{
		if (a_rules.rows.empty() || (a_inCombat && !SituationRules::AppliesInCombat(a_rules))) {
			return false;
		}
		return std::ranges::all_of(a_rules.rows, [&a_facts](const SituationRules::Row& a_row) { return RowHolds(a_row, a_facts); });
	}

	// A werewolf or Vampire Lord, or a mod's transformation: their races are not playable
	// (Tailor's InBeastForm). The transformation takes the gear off and puts it back on.
	bool IsPlayerInBeastForm(RE::Actor& a_actor)
	{
		const auto* race = a_actor.IsPlayerRef() ? a_actor.GetRace() : nullptr;
		return race && !race->data.flags.all(RE::RACE_DATA::Flag::kPlayable);
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
		std::erase_if(g_inWater, [&actors](RE::FormID a_actor) { return std::ranges::find(actors, a_actor) == actors.end(); });
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
		const auto wanted = [&a_rows](Situation a_situation) { return OutfitRows::HoldsAny(a_rows[OutfitRows::RowOf(a_situation)]); };
		if (std::ranges::none_of(Priority(), wanted)) {
			return applicable;  // nothing to read: no situation they could be dressed for
		}
		// A fight is fought in the default outfit, unless a user's situation applies in combat.
		const bool inCombat = a_actor.IsInCombat();
		Facts      facts{ .actor = a_actor };
		for (const auto situation : Priority()) {
			if (!wanted(situation)) {
				continue;
			}
			if (IsUser(situation)) {
				const auto* rules = SituationStore::RulesOf(situation);
				if (rules && RulesHold(*rules, facts, inCombat)) {
					applicable.Add(situation);
				}
			} else if (!inCombat && Holds(ConditionOf(situation), a_actor, facts.Location())) {
				applicable.Add(situation);
			}
		}
		return applicable;
	}

	void Start()
	{
		g_running = true;
		if (!g_poller.joinable()) {
			g_poller = std::jthread(RunPoller);
		}
	}

	void Stop()
	{
		g_running = false;
		g_lastSeen.clear();
		g_lastRedressed.clear();
		g_inWater.clear();
	}
}
