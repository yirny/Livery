// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Dresses an actor in a set of armor by handing them the items and equipping them,
// never through their outfit record (docs/design.md §2). With the lock, each worn
// piece gets PreventRemoval (ExtraCannotWear), so the engine's best-gear pass leaves
// that slot alone and better armor given later stays in the inventory.
//
// Contract: every function runs on the game thread (SKSE task). The inventory is read
// again after every native call; an instance is found again only by the ExtraUniqueID
// this module gives it.
namespace Dressing
{
	// One inventory instance of an armor: the armor and the ExtraUniqueID on its extra list.
	struct InstanceKey
	{
		RE::FormID    armor = 0;
		RE::FormID    baseID = 0;  // ExtraUniqueID::baseID: the actor who got the tag
		std::uint16_t uniqueID = 0;

		friend bool operator==(const InstanceKey&, const InstanceKey&) = default;
	};

	// A piece an actor wears for their outfit, and what this module changed on it.
	struct Piece
	{
		InstanceKey key;
		bool        added = false;   // this module added the instance: Release can take it back
		bool        locked = false;  // this module put PreventRemoval on it: Release takes that off
	};

	struct Result
	{
		std::vector<Piece>              worn;
		std::vector<RE::TESObjectARMO*> notCarried;  // left out: the actor had none and adding was off
		std::vector<RE::TESObjectARMO*> refused;     // the engine would not put it on (race, a slot another mod locked)
	};

	// Puts `outfit` on them, per piece the instance they carry (worn, else enchanted,
	// else the most tempered, else plain), else a new plain copy when `addMissing`.
	// With `lock`, every piece gets PreventRemoval. Worn armor outside the outfit comes
	// off, except non-playable armor and pieces another mod locked.
	Result Dress(RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& outfit, bool lock, bool addMissing);

	// Puts on again, with PreventRemoval, the pieces they still carry but no longer wear
	// (another mod took them off, which clears the lock). A piece they no longer carry
	// is left alone. Updates each piece's `locked`; returns how many went back on.
	std::size_t PutBackOn(RE::Actor& actor, std::vector<Piece>& pieces);

	// Takes off the locks this module set; with `removeCopies`, also takes back the
	// copies it added. Instances that were the actor's own stay with them.
	void Release(RE::Actor& actor, const std::vector<Piece>& pieces, bool removeCopies);

	// The playable armor they wear, each form once.
	std::vector<RE::TESObjectARMO*> WornArmor(RE::Actor& actor);
}
