// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - the poll interval and redress hold, the water reach and leave, the snow share
//   Detection - water, sleep, cold outdoors, location, the player's beast form
//   Poll      - a thread queues one SKSE task per interval; the task evaluates every
//               assigned actor with situation outfits, the player included, and
//               redresses at most one
//   API       - Evaluate / Start / Stop
//
// The rules follow Tailor 3.0's src/events/SituationHandler.cpp and its policy headers
// (SwimmingPolicy, SleepStatePolicy, WarmPolicy, WarmWeather, LocationSituationPolicy).
#include "PCH.h"

#include "Situations.h"

#include "Assignments.h"
#include "CaptureWindow.h"
#include "OutfitPreview.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string_view>
#include <thread>
#include <unordered_map>

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
	// Once the poll saw them in water, they stay in it until the water is this far under
	// their feet, so walking along a shore does not flip Swimming on and off.
	constexpr float kWaterLeave = 60.0f;  // game units
	// A snowy region: snow weathers hold at least one part in this many of its weather chance.
	constexpr std::uint64_t kSnowShare = 4;

	// --- Detection ------------------------------------------------------------
	//
	// Contract: game thread, all.

	// `a_wasIn`: the poll last saw them in water, so they leave it only past kWaterLeave.
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

	// Outdoors in cloudy, rainy or snowy weather, or anywhere outdoors in a snowy region
	// (whose clear days are its own). While the sky blends from the last weather to the
	// current one, the last one decides until the new one is halfway in, so nobody takes a
	// cloak off while the snow still falls.
	bool IsColdOutdoors(RE::Actor& a_actor)
	{
		using Flag = RE::TESWeather::WeatherDataFlag;
		const auto* cell = a_actor.GetParentCell();
		if (!cell || !cell->IsExteriorCell()) {
			return false;
		}
		auto* sky = RE::Sky::GetSingleton();
		if (!sky) {
			return false;
		}
		const bool  lastDecides = sky->lastWeather && sky->currentWeatherPct < 0.5f;
		const auto* weather = lastDecides ? sky->lastWeather : sky->currentWeather;
		if (weather && weather->data.flags.any(Flag::kCloudy, Flag::kRainy, Flag::kSnow)) {
			return true;
		}
		return sky->region && IsSnowyRegion(*sky->region);
	}

	// Home is any house for an NPC and their own for the player; a city, town, settlement,
	// dwelling or inn is Town, and so is a home, which falls back to the Town outfit.
	// Keywords keep their editor IDs at runtime, so they are named rather than numbered.
	void AddByLocation(RE::Actor& a_actor, Situations::Applicable& a_applicable)
	{
		const auto* location = a_actor.GetCurrentLocation();
		if (!location) {
			return;
		}
		const auto has = [location](std::string_view a_keyword) { return location->HasKeywordString(a_keyword); };
		const bool home = has("LocTypePlayerHouse"sv) || (!a_actor.IsPlayerRef() && has("LocTypeHouse"sv));
		const bool town = home || has("LocTypeCity"sv) || has("LocTypeTown"sv) || has("LocTypeSettlement"sv) || has("LocTypeDwelling"sv) ||
		                  has("LocTypeInn"sv);
		if (home) {
			a_applicable.Add(Situations::Situation::kHome);
		}
		if (town) {
			a_applicable.Add(Situations::Situation::kTown);
		}
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

	bool WasSeenSwimming(RE::FormID a_actor)
	{
		const auto seen = g_lastSeen.find(a_actor);
		if (seen == g_lastSeen.end()) {
			return false;
		}
		const auto& items = seen->second.items;
		return std::ranges::find(items.begin(), items.begin() + seen->second.count, Situations::Situation::kSwimming) != items.begin() + seen->second.count;
	}

	bool IsHeld(RE::FormID a_actor, std::chrono::steady_clock::time_point a_now)
	{
		const auto last = g_lastRedressed.find(a_actor);
		return last != g_lastRedressed.end() && a_now - last->second < kRedressHold;
	}

	void Forget(RE::FormID a_actor)
	{
		g_lastSeen.erase(a_actor);
		g_lastRedressed.erase(a_actor);
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
		const auto time = std::chrono::steady_clock::now();
		bool       redressed = false;
		for (const auto id : actors) {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!actor || !actor->Is3DLoaded() || actor->IsDead() || IsPlayerInBeastForm(*actor)) {
				Forget(id);  // seen anew when they load again or turn back
				continue;
			}
			const auto now = Situations::Evaluate(*actor);
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
	Applicable Evaluate(RE::Actor& a_actor)
	{
		Applicable applicable;
		if (a_actor.IsInCombat()) {
			return applicable;  // a fight is fought in the default outfit
		}
		if (IsInWater(a_actor, WasSeenSwimming(a_actor.GetFormID()))) {
			applicable.Add(Situation::kSwimming);
		}
		if (IsSleeping(a_actor)) {
			applicable.Add(Situation::kSleep);
		}
		if (IsColdOutdoors(a_actor)) {
			applicable.Add(Situation::kWarm);
		}
		AddByLocation(a_actor, applicable);
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
	}
}
