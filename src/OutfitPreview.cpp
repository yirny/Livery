// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State     - the previewed actor, the pieces shown, what Begin found, the locks the preview set
//   Inventory - armor copies with extra data, found again by ExtraUniqueID; the lock
//   Dressing  - Equip / Unequip / TakeBack by key, the hands; Dress picks a copy per piece
//               and puts it on after the rest came off; Restore puts back what Begin found
//   API       - Begin / SetPiece / Show / End / Abandon / IsActive
//
// The character is dressed through their inventory, never through their outfit record:
// the engine's outfit swap removes every copy of the old outfit's pieces, the NPC's own
// ones and Livery's locked ones included. The path follows Tailor 3.0 (GPL-3.0), its
// src/player/PlayerWardrobe.cpp, PlayerHands.cpp and PlayerModel.cpp (Tag, DressItems,
// BeginPreview, EndPreview, ReturnHands, RefreshPlayerModel), used for NPCs too: a piece
// shown on an NPC is locked (ExtraCannotWear, the game's PreventRemoval) while it is
// shown, so the engine's best-gear pass leaves it on, and the locks Livery itself put on
// an assigned NPC are off for the preview (Assignments::SuspendLocks).
#include "PCH.h"

#include "OutfitPreview.h"

#include "Assignments.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

// wingdi.h rewrites any GetObject call to GetObjectA/W; BGSDefaultObjectManager::GetObject is not one.
#undef GetObject

namespace
{
	// --- State ----------------------------------------------------------------

	// One inventory copy, as Tailor's wardrobe identifies it: the armor and the
	// ExtraUniqueID given to its extra list. Native calls can rebuild extra lists,
	// so a copy is found again by key, never by pointer.
	struct CopyKey
	{
		RE::FormID    form = 0;
		RE::FormID    owner = 0;
		std::uint16_t unique = 0;

		friend bool operator==(const CopyKey&, const CopyKey&) = default;
	};

	// What they wore and held as the preview began, and what the preview did since.
	struct Snapshot
	{
		std::vector<CopyKey>        worn;
		std::uint32_t               lockedSlots = 0;  // body slots of worn pieces another mod locked: the preview leaves those alone
		std::array<RE::TESForm*, 2> hands{};          // what the left and right hands held, armor aside: a previewed shield takes them
		std::vector<CopyKey>        added;            // copies the preview put in the inventory; End takes them back
		std::vector<CopyKey>        locked;           // copies the preview locked (an NPC's); Unequip and End unlock them
	};

	// Contract: game thread only. One preview at a time.
	bool                            g_active = false;
	RE::ActorHandle                 g_actor;
	RE::FormID                      g_actorID = 0;  // for Assignments, which needs it once the actor may be gone
	bool                            g_isPlayer = false;
	std::vector<RE::TESObjectARMO*> g_pieces;
	Snapshot                        g_snapshot;

	// --- Inventory ------------------------------------------------------------

	struct Copy
	{
		RE::TESObjectARMO* armor = nullptr;
		RE::ExtraDataList* extra = nullptr;
	};

	// Every armor copy with extra data, optionally of one armor. Plain copies have
	// no extra list and are only counted by the engine. Native calls can rebuild
	// extra lists, so the copies are read again after any call that changes the inventory.
	std::vector<Copy> Copies(RE::Actor& actor, const RE::TESObjectARMO* only = nullptr)
	{
		std::vector<Copy> copies;
		auto*             changes = actor.GetInventoryChanges();
		if (!changes || !changes->entryList) {
			return copies;
		}
		for (auto* entry : *changes->entryList) {
			auto* armor = entry && entry->object ? entry->object->As<RE::TESObjectARMO>() : nullptr;
			if (!armor || (only && armor != only) || !entry->extraLists) {
				continue;
			}
			for (auto* extra : *entry->extraLists) {
				if (extra) {
					copies.push_back({ armor, extra });
				}
			}
		}
		return copies;
	}

	std::int32_t CountOf(RE::Actor& actor, RE::TESObjectARMO& armor)
	{
		const auto inventory = actor.GetInventory([&armor](RE::TESBoundObject& item) { return &item == &armor; });
		const auto found = inventory.find(&armor);
		return found != inventory.end() ? found->second.first : 0;
	}

	// Non-playable armor (skin, body-mod parts) is never gear.
	bool IsGear(const RE::TESObjectARMO& armor)
	{
		return (armor.formFlags & RE::TESObjectARMO::RecordFlags::kNonPlayable) == 0;
	}

	std::optional<CopyKey> KeyOf(const Copy& copy)
	{
		const auto* id = copy.extra->GetByType<RE::ExtraUniqueID>();
		if (!id) {
			return std::nullopt;
		}
		return CopyKey{ copy.armor->GetFormID(), id->baseID, id->uniqueID };
	}

	// Gives the copy an ExtraUniqueID when it has none, so it is found again as
	// itself after any number of native calls.
	CopyKey Tag(RE::Actor& actor, const Copy& copy)
	{
		if (const auto key = KeyOf(copy)) {
			return *key;
		}
		auto* id = new RE::ExtraUniqueID(actor.GetFormID(), actor.GetInventoryChanges()->GetNextUniqueID());
		copy.extra->Add(id);
		actor.AddChange(RE::TESObjectREFR::ChangeFlags::kInventory);
		return { copy.armor->GetFormID(), id->baseID, id->uniqueID };
	}

	Copy Find(RE::Actor& actor, const CopyKey& key)
	{
		for (const auto& copy : Copies(actor)) {
			if (const auto found = KeyOf(copy); found && *found == key) {
				return copy;
			}
		}
		return {};
	}

	bool Contains(const std::vector<CopyKey>& keys, const CopyKey& key)
	{
		return std::ranges::find(keys, key) != keys.end();
	}

	// ExtraCannotWear is the game's PreventRemoval: on a worn copy it keeps the piece
	// on, on an unworn one it keeps the copy from being equipped.
	bool IsLocked(const RE::ExtraDataList& extra)
	{
		return extra.HasType<RE::ExtraCannotWear>();
	}

	// Contract: the copy is worn and has no lock yet.
	void Lock(RE::Actor& actor, RE::ExtraDataList& extra)
	{
		extra.Add(new RE::ExtraCannotWear());
		actor.AddChange(RE::TESObjectREFR::ChangeFlags::kInventory);
	}

	void Unlock(RE::Actor& actor, RE::ExtraDataList& extra)
	{
		extra.RemoveByType(RE::ExtraDataType::kCannotWear);
		actor.AddChange(RE::TESObjectREFR::ChangeFlags::kInventory);
	}

	// --- Dressing -------------------------------------------------------------

	// The worn gear, each copy tagged.
	std::vector<CopyKey> WornGear(RE::Actor& actor)
	{
		std::vector<CopyKey> keys;
		for (const auto& copy : Copies(actor)) {
			if (copy.extra->GetWorn() && IsGear(*copy.armor)) {
				keys.push_back(Tag(actor, copy));
			}
		}
		return keys;
	}

	// The body slots of the worn gear another mod locked.
	// Contract: after Assignments took Livery's own locks off, so every lock left is another mod's.
	std::uint32_t LockedSlots(RE::Actor& actor)
	{
		std::uint32_t slots = 0;
		for (const auto& copy : Copies(actor)) {
			if (copy.extra->GetWorn() && IsGear(*copy.armor) && IsLocked(*copy.extra)) {
				slots |= copy.armor->GetSlotMask().underlying();
			}
		}
		return slots;
	}

	// The worn copy of this armor, tagged; nullopt when none is worn.
	std::optional<CopyKey> TagWorn(RE::Actor& actor, const RE::TESObjectARMO& armor)
	{
		for (const auto& copy : Copies(actor, &armor)) {
			if (copy.extra->GetWorn()) {
				return Tag(actor, copy);
			}
		}
		return std::nullopt;
	}

	// Notes the lock on the copy as the preview's, so Unequip and End take it off
	// again. Contract: the copy had no lock before the preview put it on.
	void NoteLock(RE::Actor& actor, const CopyKey& key)
	{
		if (const auto copy = Find(actor, key); copy.extra && IsLocked(*copy.extra) && !Contains(g_snapshot.locked, key)) {
			g_snapshot.locked.push_back(key);
		}
	}

	// Puts the copy on, with the lock when `lock`: forceEquip is PreventRemoval, and
	// a copy already worn gets the flag directly (as Dressing does). A lock already
	// there is another mod's and is left as it is.
	void Equip(RE::Actor& actor, const CopyKey& key, bool lock)
	{
		const auto copy = Find(actor, key);
		if (!copy.extra) {
			return;
		}
		const bool wasLocked = IsLocked(*copy.extra);
		if (!copy.extra->GetWorn()) {
			RE::ActorEquipManager::GetSingleton()->EquipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, lock, false, true);
		} else if (lock && !wasLocked) {
			Lock(actor, *copy.extra);
		}
		if (lock && !wasLocked) {
			NoteLock(actor, key);
		}
	}

	// Takes the copy off, its preview lock first: the engine keeps a locked piece on.
	// A piece another mod locked stays: only that mod should take it off.
	void Unequip(RE::Actor& actor, const CopyKey& key)
	{
		auto copy = Find(actor, key);
		if (!copy.extra || !copy.extra->GetWorn()) {
			return;
		}
		if (Contains(g_snapshot.locked, key)) {
			Unlock(actor, *copy.extra);
			std::erase(g_snapshot.locked, key);
			copy = Find(actor, key);  // read again after the change
		}
		if (!copy.extra || IsLocked(*copy.extra)) {
			return;
		}
		RE::ActorEquipManager::GetSingleton()->UnequipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
	}

	// Removes exactly this copy, worn or not; one already gone is simply gone.
	void TakeBack(RE::Actor& actor, const CopyKey& key)
	{
		Unequip(actor, key);
		if (const auto copy = Find(actor, key); copy.armor) {
			actor.RemoveItem(copy.armor, 1, RE::ITEM_REMOVE_REASON::kRemove, copy.extra, nullptr);
		}
	}

	// Takes off every lock the preview set and still holds.
	void UnlockAll(RE::Actor& actor)
	{
		for (const auto& key : g_snapshot.locked) {
			if (const auto copy = Find(actor, key); copy.extra && IsLocked(*copy.extra)) {
				Unlock(actor, *copy.extra);
			}
		}
		g_snapshot.locked.clear();
	}

	// What a hand holds, unless it is armor: a shield is gear and goes with the worn keys.
	RE::TESForm* HeldIn(RE::Actor& actor, bool leftHand)
	{
		auto* held = actor.GetEquippedObject(leftHand);
		return held && !held->Is(RE::FormType::Armor) ? held : nullptr;
	}

	// Puts what a hand held back into it when the hand is empty now: a previewed
	// shield took it (both hands for a bow or a two-handed weapon).
	void ReturnHand(RE::Actor& actor, bool leftHand, RE::TESForm* held)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
		if (!held || !equipManager || !defaults || actor.GetEquippedObject(leftHand)) {
			return;
		}
		const auto* slot = defaults->GetObject<RE::BGSEquipSlot>(leftHand ? RE::DEFAULT_OBJECT::kLeftHandEquip : RE::DEFAULT_OBJECT::kRightHandEquip);
		if (auto* spell = held->As<RE::SpellItem>()) {
			equipManager->EquipSpell(&actor, spell, slot);
		} else if (auto* object = held->As<RE::TESBoundObject>()) {
			equipManager->EquipObject(&actor, object, nullptr, 1, slot, false, false, false, true);
		}
	}

	bool IsShown(RE::FormID form)
	{
		return std::ranges::any_of(g_pieces, [form](const RE::TESObjectARMO* armor) { return armor->GetFormID() == form; });
	}

	// Dresses them in g_pieces. Worn pieces the look no longer has come off first: on
	// an NPC they are locked, and the engine is never asked to swap a locked piece
	// out. Then each piece goes on through a copy already worn, else the one worn as
	// the preview began, else a copy the preview added, else any copy they own, else
	// a copy the preview adds now; a piece for a slot another mod locked is left out,
	// unless it is the locked piece itself. Preview copies the look no longer uses go
	// back. Tailor's DressItems.
	// Contract: game thread; their own copies are never removed.
	void Dress(RE::Actor& actor)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return;
		}
		const bool lock = !g_isPlayer;  // the player cannot take a locked piece off in the inventory menu
		const auto worn = WornGear(actor);
		for (const auto& key : worn) {
			if (!IsShown(key.form)) {
				Unequip(actor, key);
			}
		}
		const std::array<const std::vector<CopyKey>*, 3> known{ &worn, &g_snapshot.worn, &g_snapshot.added };
		std::vector<CopyKey>                             chosen;
		for (auto* armor : g_pieces) {
			const auto form = armor->GetFormID();
			if (std::ranges::any_of(chosen, [form](const CopyKey& key) { return key.form == form; })) {
				continue;
			}
			std::optional<CopyKey> pick;
			for (const auto* keys : known) {
				for (const auto& key : *keys) {
					if (key.form == form && Find(actor, key).armor) {
						pick = key;
						break;
					}
				}
				if (pick) {
					break;
				}
			}
			if ((armor->GetSlotMask().underlying() & g_snapshot.lockedSlots) && !(pick && Contains(worn, *pick))) {
				logger::info("Preview leaves armor {:08X} out: a piece another mod locked has its slot", form);
				continue;
			}
			if (pick) {
				Equip(actor, *pick, lock);
			} else if (CountOf(actor, *armor) > 0) {
				// Plain copies carry no extra data: the equip manager picks one.
				equipManager->EquipObject(&actor, armor, nullptr, 1, nullptr, false, lock, false, true);
				pick = TagWorn(actor, *armor);
			} else {
				actor.AddObjectToContainer(armor, nullptr, 1, nullptr);
				equipManager->EquipObject(&actor, armor, nullptr, 1, nullptr, false, lock, false, true);
				pick = TagWorn(actor, *armor);
				if (pick) {
					g_snapshot.added.push_back(*pick);
				} else {
					actor.RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);  // refused (race, a locked slot): the copy goes straight back
				}
			}
			if (pick) {
				if (lock) {
					NoteLock(actor, *pick);  // the engine put the flag on the copy it equipped with forceEquip
				}
				chosen.push_back(*pick);
			} else {
				logger::warn("Preview could not put armor {:08X} on {:08X}", form, actor.GetFormID());
			}
		}
		std::vector<CopyKey> kept;
		for (const auto& key : g_snapshot.added) {
			if (Contains(chosen, key)) {
				kept.push_back(key);
			} else {
				TakeBack(actor, key);
			}
		}
		g_snapshot.added = std::move(kept);
	}

	// Puts back exactly what they wore as the preview began. Their own copies were
	// never removed and the same extra lists go on again, so tempering, enchantments
	// and names keep. The preview's locks go last: a piece worn at both ends stays on
	// throughout. Livery's own locks on an assigned NPC are Assignments' to put back.
	void Restore(RE::Actor& actor)
	{
		for (const auto& key : WornGear(actor)) {
			if (!Contains(g_snapshot.worn, key)) {
				Unequip(actor, key);
			}
		}
		for (const auto& key : g_snapshot.added) {
			TakeBack(actor, key);
		}
		for (const auto& key : g_snapshot.worn) {
			Equip(actor, key, false);  // nothing for a copy that is gone
		}
		UnlockAll(actor);
		ReturnHand(actor, true, g_snapshot.hands[0]);
		ReturnHand(actor, false, g_snapshot.hands[1]);
	}

	// Rebuilds the player's model now: equip calls alone leave it as it was until
	// something else rebuilds it, so the preview would lag behind the ticks. An NPC
	// needs none: the actor update applies their equips while the game runs.
	// Tailor's RefreshPlayerModel. CommonLibSSE-NG's Update3DModel also sends SKSE's
	// NiNodeUpdate event, so body-morph mods refit the new pieces.
	void RefreshModel(RE::Actor& player)
	{
		auto* process = player.Is3DLoaded() ? player.GetActorRuntimeData().currentProcess : nullptr;
		if (!process) {
			return;
		}
		process->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kModel);
		process->Update3DModel(&player);
	}

	// --- API ------------------------------------------------------------------

	void Redress(RE::Actor& actor)
	{
		Dress(actor);
		if (g_isPlayer) {
			RefreshModel(actor);
		}
	}

	// How many of the pieces shown the character wears, for the log.
	std::size_t WornCount(RE::Actor& actor)
	{
		const auto copies = Copies(actor);
		return static_cast<std::size_t>(std::ranges::count_if(g_pieces, [&copies](const RE::TESObjectARMO* armor) {
			return std::ranges::any_of(copies, [armor](const Copy& copy) { return copy.armor == armor && copy.extra->GetWorn(); });
		}));
	}

	void ClearState()
	{
		g_actorID = 0;
		g_pieces.clear();
		g_snapshot = {};
	}
}

namespace OutfitPreview
{
	std::string Begin(RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& pieces)
	{
		End();  // a preview still on goes back first
		g_isPlayer = actor.IsPlayerRef();
		g_actorID = actor.GetFormID();
		actor.InitInventoryIfRequired();
		// An assigned NPC wears Livery's pieces locked: the locks go off so the preview
		// can take the pieces off, and back on at End.
		Assignments::SuspendLocks(actor);
		g_snapshot = { .worn = WornGear(actor), .lockedSlots = LockedSlots(actor), .hands = { HeldIn(actor, true), HeldIn(actor, false) } };
		g_active = true;
		g_actor = actor.GetHandle();
		g_pieces = pieces;
		Redress(actor);
		logger::info("Preview on {:08X}: {} of {} piece(s) worn", actor.GetFormID(), WornCount(actor), g_pieces.size());
		return {};
	}

	void SetPiece(RE::TESObjectARMO* armor, bool on)
	{
		const auto actor = g_actor.get();
		if (!g_active || !actor || !armor) {
			return;
		}
		const auto found = std::ranges::find(g_pieces, armor);
		if (on == (found != g_pieces.end())) {
			return;  // already so
		}
		if (on) {
			g_pieces.push_back(armor);
		} else {
			g_pieces.erase(found);
		}
		Redress(*actor);
	}

	void Show(const std::vector<RE::TESObjectARMO*>& pieces)
	{
		const auto actor = g_actor.get();
		if (!g_active || !actor) {
			return;
		}
		g_pieces = pieces;
		Redress(*actor);
	}

	void End()
	{
		if (!g_active) {
			return;
		}
		g_active = false;
		const auto actor = std::exchange(g_actor, {}).get();
		if (actor) {
			Restore(*actor);
			if (g_isPlayer) {
				RefreshModel(*actor);
			}
		} else {
			logger::warn("Preview off: the character is gone, so what they wore is not put back");
		}
		// The assignment's locks go back on, the actor gone or not: the record stays, and
		// nothing is put back on them while the locks are off.
		Assignments::ResumeLocks(g_actorID);
		logger::info("Preview off");
		ClearState();
	}

	void Abandon()
	{
		if (!g_active) {
			return;
		}
		g_active = false;
		g_actor = {};
		ClearState();
	}

	bool IsActive()
	{
		return g_active;
	}
}
