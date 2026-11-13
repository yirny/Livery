// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RE
{
	class Actor;
}

namespace OutfitRows
{
	class Rows;
}

namespace SituationRules
{
	struct FormRef;
	struct PlaceChoices;
	struct SpellChoice;
}

// The situations an actor can have an outfit for besides their default (docs/design.md §7):
// the presets with fixed rules and those the user defines (docs/custom-situations.md), how
// the ones an actor is in are told, and the poll that redresses assigned actors, the player
// included, when they change (Assignments::WearFor).
//
// Contract: the registry and Evaluate are the game thread's. Start and Stop run from the
// SKSE message handler, on the game thread; the poll thread touches no game state and only
// queues SKSE tasks.
namespace Situations
{
	// A situation's id: its index in the registry (Count), the Default first. The presets
	// are named here; the ids past them are the user's situations (AddUser). Never stored:
	// the co-save and situations.json keep a situation by its key (Name), so situations can
	// be added, removed or reordered.
	enum class Situation : std::uint8_t
	{
		kDefault,  // worn where no chosen situation applies; never chosen itself
		kTown,
		kCastle,
		kHome,
		kSleep,
		kCold
	};

	// The most situations the registry holds, the Default and the presets included, so
	// Applicable holds every situation an actor is in without an allocation on the poll.
	inline constexpr std::size_t kMaxSituations = 64;
	// The presets, the Default included: the ids up to kCold.
	inline constexpr std::size_t kPresetCount = static_cast<std::size_t>(Situation::kCold) + 1;

	// What a preset's fixed rule tests (ConditionOf).
	enum class Condition : std::uint8_t
	{
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
			std::string key;                           // Name
			Condition   condition = Condition::kTown;  // a preset's ConditionOf; unused for the Default and the user's
			const char* conditionsText = "";           // a preset's ConditionsText
			bool        user = false;                  // IsUser
		};

		struct Registry
		{
			std::vector<Definition> definitions;  // at the index of their id
			std::vector<Situation>  choosable;    // Choosable
			std::vector<Situation>  priority;     // Priority
		};

		// Contract: the game thread's (the unit tests add nothing). Each list is reserved for
		// kMaxSituations and never grows past it, so adding a situation moves none: the keys
		// and spans handed out stay valid, a span holding the situations there were as it was
		// taken. A situation is never taken out, so ids are never reused while the game runs.
		inline Registry& Get()
		{
			static Registry registry = [] {
				Registry built;
				built.definitions.reserve(kMaxSituations);
				built.choosable.reserve(kMaxSituations);
				built.priority.reserve(kMaxSituations);
				built.definitions.push_back({ .key = "Default", .conditionsText = "Worn where no situation applies,\nand in combat." });
				built.definitions.push_back({ .key = "Town", .condition = Condition::kTown, .conditionsText = "A city, town, settlement, inn or other inhabited interior." });
				built.definitions.push_back({ .key = "Castle", .condition = Condition::kCastle, .conditionsText = "A jarl's palace or keep." });
				built.definitions.push_back({ .key = "Home", .condition = Condition::kHome, .conditionsText = "A house." });
				built.definitions.push_back({ .key = "Sleep", .condition = Condition::kSleeping, .conditionsText = "Going to bed, in it or getting up." });
				built.definitions.push_back(
					{ .key = "Cold", .condition = Condition::kCold, .conditionsText = "Outdoors in rain or snow, or anywhere outdoors in a snowy region." });
				// insert, not assignment: it keeps the reserved capacity.
				built.choosable.insert(built.choosable.end(), { Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kSleep, Situation::kCold });
				// A home and a castle are in a town too, so one without an outfit of its own falls
				// back to the Town one.
				built.priority.insert(built.priority.end(), { Situation::kSleep, Situation::kCold, Situation::kHome, Situation::kCastle, Situation::kTown });
				return built;
			}();
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
	// (OutfitRows::RowOf): the presets, then the user's in the order added.
	inline std::span<const Situation> Choosable()
	{
		return Detail::Get().choosable;
	}

	// The order Evaluate tells the situations in: the user's first, in the order added, then
	// the presets, the most specific first. Where the user's stand is provisional
	// (docs/custom-situations.md §8).
	inline std::span<const Situation> Priority()
	{
		return Detail::Get().priority;
	}

	// A situation the user defined (AddUser), whose rules SituationStore keeps.
	inline bool IsUser(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).user;
	}

	// Adds a situation the user defines under `a_key` (`user:<n>`, a key the registry does not
	// hold): last in Choosable(), and in Priority() after the user's added before it, ahead of
	// the presets. nullopt when the registry holds kMaxSituations already.
	// Contract: game thread; only SituationStore adds, so its table keeps the registry's ids.
	inline std::optional<Situation> AddUser(std::string a_key)
	{
		auto& registry = Detail::Get();
		if (registry.definitions.size() >= kMaxSituations) {
			return std::nullopt;
		}
		const auto situation = static_cast<Situation>(registry.definitions.size());
		const auto usersBefore = registry.definitions.size() - kPresetCount;
		registry.definitions.push_back({ .key = std::move(a_key), .user = true });
		registry.choosable.push_back(situation);
		registry.priority.insert(registry.priority.begin() + static_cast<std::ptrdiff_t>(usersBefore), situation);
		return situation;
	}

	// Contract: a preset other than kDefault, which applies where no other situation does.
	inline Condition ConditionOf(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).condition;
	}

	// The situation's key: "Town", "Castle", ..., "user:3"; "Default". situations.json and the
	// co-save store a situation under it and the logs name it by it; the window and the HUD show
	// the name SituationStore keeps.
	inline const char* Name(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).key.c_str();
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

	// When a preset applies, in the README's words; the Default's says where it is worn; ""
	// for the user's, whose Applies line SituationRules::Text words from their rules.
	// the Situations page shows it read only (docs/design.md §8);
	// the Default's it draws itself with the situations' star (DrawDefaultConditions), worded so.
	inline const char* ConditionsText(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).conditionsText;
	}

	// The situations an actor is in now, the most specific first: the first one they have
	// an outfit for is the one worn (Assignments). Anywhere none applies, empty; in combat,
	// only the user's that apply in combat.
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

	// The situations of `a_rows` (the actor's rows) whose row holds an outfit and that apply
	// now: a situation they have no outfit for cannot be worn, so its conditions are not read.
	// A situation in water ends later for an actor last told in it (hysteresis on a shore).
	// Contract: game thread.
	Applicable Evaluate(RE::Actor& a_actor, const OutfitRows::Rows& a_rows);

	// The places the Add place popup offers for `a_actor` (SituationRules::PlaceChoices): the
	// location and cell they stand in, and the locations that location is in. Read as the window
	// opens, which freezes the game.
	// Contract: game thread.
	SituationRules::PlaceChoices PlacesAround(RE::Actor& a_actor);

	// The spells the Add spell popup offers for `a_actor`: the spells, powers and shouts they
	// cast (learned, and of their NPC record and race, a shout by its words) and the items behind
	// the visible effects on them now; each with a name and a visible effect, by name, one of each
	// with the same name and effects (SituationRules::MergeSameSpells). Read as the window opens.
	// Contract: game thread.
	std::vector<SituationRules::SpellChoice> SpellsAround(RE::Actor& a_actor);

	// Every spell, potion, scroll and ingredient of the loaded plugins with a name and a visible
	// effect, as SpellsAround lists them: what the Add spell popup offers with Show all spells.
	// Built at the first call, kept until the game closes.
	// Contract: game thread, after kDataLoaded.
	std::span<const SituationRules::SpellChoice> AllSpells();

	// The visible effects of the spell, potion, scroll or ingredient `a_formID` names
	// (SituationRules::FormRef::effects); empty for 0 or a form that is none of them.
	// Contract: game thread, after kDataLoaded.
	std::vector<std::uint32_t> VisibleEffectsOf(std::uint32_t a_formID);

	// The poll that keeps assigned actors dressed for their situations: Start at
	// kPostLoadGame and kNewGame, Stop at kPreLoadGame.
	void Start();
	void Stop();
}
