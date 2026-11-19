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
	struct PlaceFact;
	struct Row;
	struct SpellChoice;
}

// The situations an actor can have an outfit for besides their default (docs/design.md §7):
// the presets and those the user defines, each a list of conditions (docs/custom-situations.md),
// in one order where the lowest that applies is worn; how the ones an actor is in are told, and
// the poll that redresses assigned actors, the player included, when they change
// (Assignments::WearFor).
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

	// The presets in the order Livery installs them, top to bottom (docs/custom-situations.md §8):
	// the lowest that applies is worn, so Sleep wins over Cold, and a home or a castle over Town.
	inline constexpr std::array<Situation, kPresetCount - 1> kInstalledOrder{ Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kCold,
		Situation::kSleep };

	namespace Detail
	{
		struct Definition
		{
			std::string key;           // Name
			bool        user = false;  // IsUser
		};

		struct Registry
		{
			std::vector<Definition> definitions;  // at the index of their id
			std::vector<Situation>  order;        // Order
		};

		// Contract: the game thread's (the unit tests add nothing). Each list is reserved for
		// kMaxSituations and never grows past it, so adding a situation or reordering moves none:
		// the keys and spans handed out stay valid, a span holding the situations there were as it
		// was taken, in the order there was. A situation is never taken out, so ids are never
		// reused while the game runs.
		inline Registry& Get()
		{
			static Registry registry = [] {
				Registry built;
				built.definitions.reserve(kMaxSituations);
				built.order.reserve(kMaxSituations);
				for (const auto* key : { "Default", "Town", "Castle", "Home", "Sleep", "Cold" }) {
					built.definitions.push_back({ .key = key });
				}
				// insert, not assignment: it keeps the reserved capacity.
				built.order.insert(built.order.end(), kInstalledOrder.begin(), kInstalledOrder.end());
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

	// Every situation but the Default, in the window's order, top to bottom: the lowest that
	// applies is worn (Evaluate). Removed ones keep their place; the window leaves them out
	// (SituationStore::Offered).
	inline std::span<const Situation> Order()
	{
		return Detail::Get().order;
	}

	// Makes `a_order` the order: every situation but the Default, each once, as SituationStore
	// keeps them. Returns false, changing nothing, for any other list.
	// Contract: game thread; only SituationStore reorders, after its write took.
	inline bool Reorder(std::span<const Situation> a_order)
	{
		auto& registry = Detail::Get();
		if (a_order.size() + 1 != registry.definitions.size()) {
			return false;
		}
		std::array<bool, kMaxSituations> seen{};
		for (const auto situation : a_order) {
			const auto index = static_cast<std::size_t>(situation);
			if (index == 0 || index >= registry.definitions.size() || seen[index]) {
				return false;
			}
			seen[index] = true;
		}
		registry.order.clear();  // keeps the reserved capacity
		registry.order.insert(registry.order.end(), a_order.begin(), a_order.end());
		return true;
	}

	// A situation the user defined (AddUser), not a preset.
	inline bool IsUser(Situation a_situation)
	{
		return Detail::DefinitionOf(a_situation).user;
	}

	// Adds a situation the user defines under `a_key` (`user:<n>`, a key the registry does not
	// hold), last in Order(), where it wins over every other. nullopt when the registry holds
	// kMaxSituations already.
	// Contract: game thread; only SituationStore adds, so its table keeps the registry's ids.
	inline std::optional<Situation> AddUser(std::string a_key)
	{
		auto& registry = Detail::Get();
		if (registry.definitions.size() >= kMaxSituations) {
			return std::nullopt;
		}
		const auto situation = static_cast<Situation>(registry.definitions.size());
		registry.definitions.push_back({ .key = std::move(a_key), .user = true });
		registry.order.push_back(situation);
		return situation;
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

	// Where the Default is worn; the Situations page draws it with the situations' star
	// (DrawDefaultConditions). Every other situation's Applies line SituationRules::Text words
	// from its rules.
	inline constexpr const char* kDefaultConditions = "Worn where no situation applies,\nand in combat.";

	// The situations an actor is in now, the lowest in Order() first: the first one they have
	// an outfit for is the one worn (Assignments). Anywhere none applies, empty; in combat,
	// only those allowed during combat.
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

	// The situations of `a_rows` (the actor's rows) whose row holds an outfit and whose rules
	// (SituationStore::RulesOf) hold now, read from the end of Order(): a situation they have no
	// outfit for cannot be worn, so its conditions are not read. A situation in water ends later
	// for an actor last told in it (hysteresis on a shore).
	// Contract: game thread; reads Order() as it is, allocating nothing.
	Applicable Evaluate(RE::Actor& a_actor, const OutfitRows::Rows& a_rows);

	// Whether each of `a_rows` (a situation's conditions as the Edit situation popup holds them)
	// is met for `a_actor` now, as Evaluate reads it; in combat or not, each row alone. Reading
	// in water moves the actor's hysteresis record as the poll's reading does.
	// Contract: game thread.
	std::vector<bool> RowsHold(RE::Actor& a_actor, std::span<const SituationRules::Row> a_rows);

	// What the never-worn mark needs of each place in `a_places` (FormIDs of locations and
	// cells; SituationRules::PlaceFact): the locations it is in, and the Location type values its
	// own keywords and those of every location within it give. One pass over the loaded
	// locations; a FormID that is no location or cell gives a fact with neither.
	// Contract: game thread, after kDataLoaded; read as the window opens, never on the poll.
	std::vector<SituationRules::PlaceFact> PlaceFacts(std::span<const std::uint32_t> a_places);

	// The places the Add place popup offers for `a_actor` (SituationRules::PlaceChoices): the
	// location and cell they stand in, and the locations that location is in. Read as the window
	// opens, which freezes the game.
	// Contract: game thread.
	SituationRules::PlaceChoices PlacesAround(RE::Actor& a_actor);

	// The spells the Add spell popup offers for `a_actor`: the spells, powers and shouts they
	// cast with an effect on themselves or a friendly one on others (learned, and of their NPC
	// record and race, a shout by its words) and the items behind the visible effects on them
	// now; each with a name and a
	// visible effect, by name, one of each with the same name and effects
	// (SituationRules::MergeSameSpells). Read as the window opens.
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
