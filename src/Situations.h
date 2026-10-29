// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace RE
{
	class Actor;
}

// The situations an actor can have an outfit for besides their default (docs/design.md §7),
// the conditions they apply on, how the ones an actor is in are told, and the poll that
// redresses assigned actors, the player included, when they change (Assignments::WearFor).
//
// Contract: Evaluate runs on the game thread. Start and Stop run from the SKSE message
// handler, on the game thread; the poll thread touches no game state and only queues
// SKSE tasks.
namespace Situations
{
	// Never stored: the co-save and situations.json keep a situation by its key (Name), so
	// situations can be added, removed or reordered here.
	enum class Situation : std::uint8_t
	{
		kDefault,  // worn where no chosen situation applies; never chosen itself
		kTown,
		kCastle,
		kHome,
		kSleep,
		kCold
	};

	// The situations an outfit can be chosen for, in the window's order, which is the enum's
	// (OutfitRows::RowOf).
	constexpr std::array kChoosable{ Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kSleep, Situation::kCold };

	// What Evaluate tells of an actor; a situation applies while its condition holds
	// (ConditionOf). No situation Livery installs applies in water: kInWater is kept for
	// situations whose conditions can be chosen (docs/design.md §7).
	enum class Condition : std::uint8_t
	{
		kInWater,
		kSleeping,
		kCold,
		kHome,
		kCastle,
		kTown
	};

	// Contract: not kDefault, which applies where no other situation does.
	constexpr Condition ConditionOf(Situation a_situation) noexcept
	{
		switch (a_situation) {
		case Situation::kCastle:
			return Condition::kCastle;
		case Situation::kHome:
			return Condition::kHome;
		case Situation::kSleep:
			return Condition::kSleeping;
		case Situation::kCold:
			return Condition::kCold;
		default:
			return Condition::kTown;
		}
	}

	// The order Evaluate tells the situations in, the most specific first. A home and a castle
	// are in a town too, so one without an outfit of its own falls back to the Town one.
	constexpr std::array kPriority{ Situation::kSleep, Situation::kCold, Situation::kHome, Situation::kCastle, Situation::kTown };

	// The situation's key: "Town", "Castle", ...; "Default". situations.json and the co-save
	// store a situation under it and the logs name it by it; the window and the HUD show the
	// name SituationStore keeps.
	constexpr const char* Name(Situation a_situation) noexcept
	{
		switch (a_situation) {
		case Situation::kTown:
			return "Town";
		case Situation::kCastle:
			return "Castle";
		case Situation::kHome:
			return "Home";
		case Situation::kSleep:
			return "Sleep";
		case Situation::kCold:
			return "Cold";
		default:
			return "Default";
		}
	}

	// A key earlier versions stored for a situation this one no longer has; what was kept
	// under it is dropped.
	constexpr bool IsRetiredKey(std::string_view a_key) noexcept
	{
		return a_key == "Swimming";
	}

	// The situation stored under `a_key` (Name), or under the key an earlier version gave it:
	// "Warm" is Cold's. nullopt for a key this version does not know or no longer has
	// (IsRetiredKey).
	constexpr std::optional<Situation> FromKey(std::string_view a_key) noexcept
	{
		for (std::size_t i = 0; i <= kChoosable.size(); ++i) {
			if (const auto situation = static_cast<Situation>(i); a_key == Name(situation)) {
				return situation;
			}
		}
		if (a_key == "Warm") {
			return Situation::kCold;
		}
		return std::nullopt;
	}

	// When the situation applies, in the README's words; the Default's says where it is worn.
	// the Situations page shows it read only, until conditions can be edited (docs/design.md §8).
	constexpr const char* ConditionsText(Situation a_situation) noexcept
	{
		switch (a_situation) {
		case Situation::kTown:
			return "A city, town, settlement, inn or other inhabited interior.";
		case Situation::kCastle:
			return "A jarl's palace or keep.";
		case Situation::kHome:
			return "A house.";
		case Situation::kSleep:
			return "Going to bed, in it or getting up.";
		case Situation::kCold:
			return "Outdoors in rain or snow, or anywhere outdoors in a snowy region.";
		default:
			return "Worn where no other situation applies.";
		}
	}

	// The situations an actor is in now, the most specific first: the first one they have
	// an outfit for is the one worn (Assignments). Empty in combat, and anywhere none
	// applies.
	struct Applicable
	{
		std::array<Situation, kChoosable.size()> items{};
		std::size_t                              count = 0;

		void Add(Situation a_situation)
		{
			items[count++] = a_situation;
		}

		friend bool operator==(const Applicable&, const Applicable&) = default;
	};

	// A situation in water ends later for an actor the poll last saw in it (hysteresis on a
	// shore). Contract: game thread.
	Applicable Evaluate(RE::Actor& a_actor);

	// The poll that keeps assigned actors dressed for their situations: Start at
	// kPostLoadGame and kNewGame, Stop at kPreLoadGame.
	void Start();
	void Stop();
}
