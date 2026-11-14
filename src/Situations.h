// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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
	// A situation's id: its index in the registry (Count), the Default first. The presets
	// are named here. Never stored: the co-save and situations.json keep a situation by its
	// key (Name), so situations can be added, removed or reordered.
	enum class Situation : std::uint8_t
	{
		kDefault,  // worn where no chosen situation applies; never chosen itself
		kTown,
		kCastle,
		kHome,
		kSleep,
		kCold
	};

	// The most situations the registry holds, the Default included, so Applicable holds every
	// situation an actor is in without an allocation on the poll.
	inline constexpr std::size_t kMaxSituations = 64;

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

	namespace Detail
	{
		struct Definition
		{
			const char* key;             // Name
			Condition   condition;       // ConditionOf; unused for the Default
			const char* conditionsText;  // ConditionsText
		};

		struct Registry
		{
			std::vector<Definition> definitions;  // at the index of their id
			std::vector<Situation>  choosable;    // Choosable
			std::vector<Situation>  priority;     // Priority
		};

		// Contract: built at the first call and never changed after, so any thread may read it
		// and the spans and keys it hands out stay valid.
		inline const Registry& Get()
		{
			static const Registry registry{
				.definitions = {
					{ "Default", Condition::kTown, "Worn where no situation applies,\nand in combat." },
					{ "Town", Condition::kTown, "A city, town, settlement, inn or other inhabited interior." },
					{ "Castle", Condition::kCastle, "A jarl's palace or keep." },
					{ "Home", Condition::kHome, "A house." },
					{ "Sleep", Condition::kSleeping, "Going to bed, in it or getting up." },
					{ "Cold", Condition::kCold, "Outdoors in rain or snow, or anywhere outdoors in a snowy region." },
				},
				.choosable = { Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kSleep, Situation::kCold },
				// A home and a castle are in a town too, so one without an outfit of its own falls
				// back to the Town one.
				.priority = { Situation::kSleep, Situation::kCold, Situation::kHome, Situation::kCastle, Situation::kTown },
			};
			return registry;
		}

		// An id the registry does not hold reads as the Default.
		inline const Definition& DefinitionOf(Situation a_situation)
		{
			const auto& definitions = Get().definitions;
			const auto  index = static_cast<std::size_t>(a_situation);
			return definitions[index < definitions.size() ? index : 0];
		}
	}

	// How many situations there are, the Default included: ids run from 0 up to it.
	inline std::size_t Count()
	{
		return Detail::Get().definitions.size();
	}

	// The situations an outfit can be chosen for, in the window's order, which is their ids'
	// (OutfitRows::RowOf).
	inline std::span<const Situation> Choosable()
	{
		return Detail::Get().choosable;
	}

	// The order Evaluate tells the situations in, the most specific first.
	inline std::span<const Situation> Priority()
	{
		return Detail::Get().priority;
	}

	// Contract: not kDefault, which applies where no other situation does.
	inline Condition ConditionOf(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).condition;
	}

	// The situation's key: "Town", "Castle", ...; "Default". situations.json and the co-save
	// store a situation under it and the logs name it by it; the window and the HUD show the
	// name SituationStore keeps.
	inline const char* Name(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).key;
	}

	// The situation stored under `a_key` (Name). nullopt for a key this version does not know.
	inline std::optional<Situation> FromKey(std::string_view a_key)
	{
		const auto& definitions = Detail::Get().definitions;
		for (std::size_t i = 0; i < definitions.size(); ++i) {
			if (a_key == definitions[i].key) {
				return static_cast<Situation>(i);
			}
		}
		return std::nullopt;
	}

	// When the situation applies, in the README's words; the Default's says where it is worn.
	// the Situations page shows it read only, until conditions can be edited (docs/design.md §8);
	// the Default's it draws itself with the situations' star (DrawDefaultConditions), worded so.
	inline const char* ConditionsText(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).conditionsText;
	}

	// The situations an actor is in now, the most specific first: the first one they have
	// an outfit for is the one worn (Assignments). Empty in combat, and anywhere none
	// applies.
	struct Applicable
	{
		std::array<Situation, kMaxSituations> items{};
		std::size_t                           count = 0;

		// Contract: each situation once, so no more than Count() of them.
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
