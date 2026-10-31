// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Dresses an actor in a set of armor by handing them the items and equipping them,
// never through their outfit record (docs/design.md §2). With the lock, each worn
// piece gets PreventRemoval (ExtraCannotWear), so the engine's best-gear pass leaves
// that slot alone and better armor given later stays in the inventory, as long as
// OwnOutfitGuard keeps the NPC's own outfit items from taking the piece off first. For the
// trade-menu filter it disguises the copies it added as outfit items (docs/design.md §3).
//
// Contract: every function but OutfitItemContestsLock runs on the game thread (SKSE task).
// The inventory is read again after every native call; an instance is found again only by
// the ExtraUniqueID this module gives it.
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
		std::vector<InstanceKey>        takenOff;    // their own copies worn before and off now; PutBack puts them on again
		std::vector<RE::TESObjectARMO*> notCarried;  // left out: the actor had none and adding was off
		std::vector<RE::TESObjectARMO*> refused;     // the engine would not put it on (race, a slot another mod locked)
	};

	// Puts `outfit` on them, per piece the instance they carry (worn, else enchanted,
	// else the most tempered, else plain), else a new plain copy when `addMissing`.
	// With `lock`, every piece gets PreventRemoval. Once a piece is on, worn armor outside
	// the outfit comes off, except non-playable armor and pieces another mod locked.
	// Every copy worn before is tagged first, so those that are off at the end, taken off
	// here or by the engine for a slot a piece filled, are listed in `takenOff`.
	Result Dress(RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& outfit, bool lock, bool addMissing);

	// Puts on again, with PreventRemoval, the pieces they still carry but no longer wear
	// (another mod took them off, which clears the lock), and locks again a worn piece
	// whose lock is gone. A piece they no longer carry is left alone. Updates each
	// piece's `locked`; returns how many it put on or locked.
	std::size_t PutBackOn(RE::Actor& actor, std::vector<Piece>& pieces);

	// Takes off worn armor that is none of the pieces and shares a body slot with a piece they
	// wear: after fast travel the engine can mark an NPC's own outfit items worn again beside
	// the locked pieces, with the 3D unchanged (docs/design.md §6). Non-playable armor and a
	// piece another mod locked stay, as in Dress. Returns how many came off.
	std::size_t TakeOffOverlapping(RE::Actor& actor, const std::vector<Piece>& pieces);

	// Whether an outfit item they carry unworn shares a body slot with a worn locked piece:
	// putting that item on would take the piece off and clear its lock (docs/design.md §6). Any
	// ExtraOutfitItem counts, since SPID's distributed outfits mark their items with another id.
	// Contract: the pass in OwnOutfitGuard calls this on a thread that is not documented, so it
	// only reads their inventory, as the pass itself does next.
	bool OutfitItemContestsLock(RE::Actor& actor);

	// Puts on again, locked, each piece that was theirs or the player's (not `added`) and
	// that they no longer carry (taken from them in a menu): a copy of that armor they carry,
	// else a new plain copy. The piece then names the copy worn. A copy this module added
	// that is gone stays gone, so taking it again and again makes no new copies. Returns how
	// many it put on.
	std::size_t Replenish(RE::Actor& actor, std::vector<Piece>& pieces);

	// Takes off the locks this module set; with `removeCopies`, also takes back the
	// copies it added. Instances that were the actor's own stay with them.
	void Release(RE::Actor& actor, const std::vector<Piece>& pieces, bool removeCopies);

	// Rebuilds their model now from what they wear. Equip calls alone leave the player's model
	// as it was until something else rebuilds it (Dress, Release and PutBack rebuild it), and
	// an NPC's until their actor update, which a frozen game skips. Nothing while their 3D is
	// not loaded.
	void RefreshModel(RE::Actor& actor);

	// Puts on again, without a lock, the copies of `keys` they still carry: their own pieces
	// Dress took off. A copy gone, already worn or locked is left alone. Returns how many
	// went on.
	std::size_t PutBack(RE::Actor& actor, const std::vector<InstanceKey>& keys);

	// Marks the copies this module added (`added`) as items of the outfit `outfit`, the way the
	// engine marks an NPC's own outfit items (ExtraOutfitItem), and returns the keys it marked;
	// a copy gone or already marked is left alone. Nothing is flagged as changed: the mark is a
	// disguise for one engine call, and UnmarkOutfitItems takes it off before anything else runs.
	// Contract: game thread; UnmarkOutfitItems with the returned keys follows in the same call.
	std::vector<InstanceKey> MarkAsOutfitItems(RE::Actor& actor, const std::vector<Piece>& pieces, RE::FormID outfit);

	// Takes the marks MarkAsOutfitItems put on the copies of `keys` off again.
	void UnmarkOutfitItems(RE::Actor& actor, const std::vector<InstanceKey>& keys);

	// The playable armor they wear, each form once.
	std::vector<RE::TESObjectARMO*> WornArmor(RE::Actor& actor);

	// Moves one copy of each armor from the player to `npc` with its extra data (enchantment,
	// tempering), as the container menu moves an item: an unworn copy, enchanted, else the most
	// tempered, else plain; the worn copy only with `allowWorn` and no other to give. Returns the
	// armor that did not reach `npc`, read from its inventory again after the moves.
	// Contract: game thread; `npc` is not the player. A moved copy is the NPC's own from then on:
	// Dress puts it on without counting it `added`, so Release leaves it with them.
	std::vector<RE::TESObjectARMO*> MoveFromPlayer(RE::Actor& player, RE::Actor& npc, const std::vector<RE::TESObjectARMO*>& armor, bool allowWorn);
}
