// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Inventory - armor copies with extra data, found again by ExtraUniqueID; the lock
//   Equip     - choose an instance per piece, put it on, take off the rest
//   API       - Dress / PutBackOn / Release / PutBack / WornArmor
//
// The inventory helpers follow src/OutfitPreview.cpp, itself from Tailor 3.0's
// src/player/PlayerWardrobe.cpp; setting ExtraCannotWear directly follows Tailor's
// src/wig/WigEquipment.cpp. docs/design.md §6 lists what was verified in game.
#include "PCH.h"

#include "Dressing.h"

#include <algorithm>
#include <optional>

namespace
{
	// --- Inventory ------------------------------------------------------------

	struct Copy
	{
		RE::TESObjectARMO* armor = nullptr;
		RE::ExtraDataList* extra = nullptr;
	};

	// Every armor copy with extra data, optionally of one armor. Plain copies have no
	// extra list and are only counted by the engine. Native calls can rebuild extra
	// lists, so the copies are read again after any call that changes the inventory.
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

	// Calls `act` on each copy `match` accepts, reading the inventory again before
	// each call because the act changes it; at most as many calls as copies matched at first.
	template <class Match, class Act>
	void ForEachCopy(RE::Actor& actor, Match match, Act act)
	{
		for (auto left = std::ranges::count_if(Copies(actor), match); left > 0; --left) {
			const auto copies = Copies(actor);
			const auto found = std::ranges::find_if(copies, match);
			if (found == copies.end()) {
				return;
			}
			act(*found);
		}
	}

	std::int32_t CountOf(RE::Actor& actor, RE::TESObjectARMO& armor)
	{
		const auto inventory = actor.GetInventory([&armor](RE::TESBoundObject& item) { return &item == &armor; });
		const auto found = inventory.find(&armor);
		return found != inventory.end() ? found->second.first : 0;
	}

	// Non-playable armor (skin, body-mod parts) is never part of an outfit.
	bool IsGear(const RE::TESObjectARMO& armor)
	{
		return (armor.formFlags & RE::TESObjectARMO::RecordFlags::kNonPlayable) == 0;
	}

	std::optional<Dressing::InstanceKey> KeyOf(const Copy& copy)
	{
		const auto* id = copy.extra->GetByType<RE::ExtraUniqueID>();
		if (!id) {
			return std::nullopt;
		}
		return Dressing::InstanceKey{ copy.armor->GetFormID(), id->baseID, id->uniqueID };
	}

	// Gives the copy an ExtraUniqueID when it has none, so it is found again as itself
	// after any number of native calls and across saves.
	Dressing::InstanceKey Tag(RE::Actor& actor, const Copy& copy)
	{
		if (const auto key = KeyOf(copy)) {
			return *key;
		}
		auto* id = new RE::ExtraUniqueID(actor.GetFormID(), actor.GetInventoryChanges()->GetNextUniqueID());
		copy.extra->Add(id);
		actor.AddChange(RE::TESObjectREFR::ChangeFlags::kInventory);
		return { copy.armor->GetFormID(), id->baseID, id->uniqueID };
	}

	Copy Find(RE::Actor& actor, const Dressing::InstanceKey& key)
	{
		for (const auto& copy : Copies(actor)) {
			if (const auto found = KeyOf(copy); found && *found == key) {
				return copy;
			}
		}
		return {};
	}

	// Their worn gear, each copy tagged.
	std::vector<Dressing::InstanceKey> WornKeys(RE::Actor& actor)
	{
		std::vector<Dressing::InstanceKey> keys;
		for (const auto& copy : Copies(actor)) {
			if (copy.extra->GetWorn() && IsGear(*copy.armor)) {
				keys.push_back(Tag(actor, copy));
			}
		}
		return keys;
	}

	// The worn copy of this armor; empty when none is worn.
	Copy WornCopy(RE::Actor& actor, const RE::TESObjectARMO& armor)
	{
		for (const auto& copy : Copies(actor, &armor)) {
			if (copy.extra->GetWorn()) {
				return copy;
			}
		}
		return {};
	}

	// ExtraCannotWear is the game's PreventRemoval: on a worn copy it keeps the piece on,
	// on an unworn one it keeps the copy from being equipped.
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

	// Rebuilds the player's model now: equip calls alone leave it as it was until
	// something else rebuilds it. Tailor's RefreshPlayerModel, as in OutfitPreview.cpp.
	void RefreshModel(RE::Actor& player)
	{
		auto* process = player.Is3DLoaded() ? player.GetActorRuntimeData().currentProcess : nullptr;
		if (!process) {
			return;
		}
		process->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kModel);
		process->Update3DModel(&player);
	}

	// --- Equip ----------------------------------------------------------------

	// An instance to dress them in; a plain copy has no extra list, and the equip
	// manager picks one of those itself.
	struct Instance
	{
		RE::ExtraDataList* extra = nullptr;  // nullptr: a plain copy
	};

	// Ranked as docs/design.md §2 says: worn, enchanted, most tempered, plain; any other
	// copy last. nullopt when they carry none. An unworn locked copy cannot be equipped
	// and is skipped.
	std::optional<Instance> PickInstance(RE::Actor& actor, RE::TESObjectARMO& armor)
	{
		RE::ExtraDataList* best = nullptr;
		bool               bestEnchanted = false;
		float              bestHealth = 0.0f;
		std::int32_t       listed = 0;  // copies on extra lists, skipped ones included
		for (const auto& copy : Copies(actor, &armor)) {
			listed += copy.extra->GetCount();
			if (copy.extra->GetWorn()) {
				return Instance{ copy.extra };
			}
			if (IsLocked(*copy.extra)) {
				continue;
			}
			const bool  enchanted = copy.extra->HasType<RE::ExtraEnchantment>();
			const auto* health = copy.extra->GetByType<RE::ExtraHealth>();
			const float tempered = health ? health->health : 1.0f;  // 1 = not tempered
			if (!best || enchanted > bestEnchanted || (enchanted == bestEnchanted && tempered > bestHealth)) {
				best = copy.extra;
				bestEnchanted = enchanted;
				bestHealth = tempered;
			}
		}
		if (best && (bestEnchanted || bestHealth > 1.0f)) {
			return Instance{ best };
		}
		if (CountOf(actor, armor) > listed) {
			return Instance{};
		}
		if (best) {
			return Instance{ best };
		}
		return std::nullopt;
	}

	enum class Outcome
	{
		kWorn,
		kNotCarried,
		kRefused
	};

	// Puts one piece on and fills `piece` when it is worn. A copy added for a piece the
	// engine refuses goes straight back.
	Outcome PutOn(RE::Actor& actor, RE::TESObjectARMO& armor, bool lock, bool addMissing, RE::ActorEquipManager& equipManager,
		Dressing::Piece& piece)
	{
		auto instance = PickInstance(actor, armor);
		bool added = false;
		if (!instance) {
			if (!addMissing) {
				return Outcome::kNotCarried;
			}
			actor.AddObjectToContainer(&armor, nullptr, 1, nullptr);
			instance = Instance{};  // they carried none, so the new plain copy is the only one
			added = true;
		}
		if (auto* extra = instance->extra; extra && extra->GetWorn()) {
			const bool lockHere = lock && !IsLocked(*extra);  // a lock already there is another mod's
			if (lockHere) {
				Lock(actor, *extra);
			}
			piece = { Tag(actor, { &armor, extra }), false, lockHere };
			return Outcome::kWorn;
		}
		// forceEquip is PreventRemoval: the engine puts ExtraCannotWear on the copy it equips.
		equipManager.EquipObject(&actor, &armor, instance->extra, 1, nullptr, false, lock, false, true);
		const auto worn = WornCopy(actor, armor);
		if (!worn.extra) {
			if (added) {
				actor.RemoveItem(&armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			return Outcome::kRefused;
		}
		// The picked copy was unlocked (PickInstance skips locked ones), so a lock now is this equip's.
		piece = { Tag(actor, worn), added, lock && IsLocked(*worn.extra) };
		return Outcome::kWorn;
	}

	// Takes off worn armor outside the outfit. Non-playable armor stays, and so does a
	// piece another mod locked (a Tailor wig, say): only that mod should release it.
	void TakeOffOthers(RE::Actor& actor, const std::vector<Dressing::Piece>& outfit, RE::ActorEquipManager& equipManager)
	{
		const auto stray = [&outfit](const Copy& copy) {
			if (!copy.extra->GetWorn() || !IsGear(*copy.armor) || IsLocked(*copy.extra)) {
				return false;
			}
			const auto key = KeyOf(copy);
			return !key || std::ranges::none_of(outfit, [&key](const Dressing::Piece& piece) { return piece.key == *key; });
		};
		ForEachCopy(actor, stray, [&](const Copy& copy) {
			equipManager.UnequipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
		});
	}
}

namespace Dressing
{
	Result Dress(RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& outfit, bool lock, bool addMissing)
	{
		Result result;
		auto*  equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return result;
		}
		// Contract: no InitInventoryIfRequired here. On an NPC who wears outfit items it took
		// ExtraWorn off every outfit copy (seen in game), and PickInstance and TakeOffOthers read it.
		const auto before = WornKeys(actor);
		for (auto* armor : outfit) {
			const auto wornAlready = [armor](const Piece& piece) { return piece.key.armor == armor->GetFormID(); };
			if (!armor || std::ranges::any_of(result.worn, wornAlready)) {
				continue;
			}
			Piece piece;
			switch (PutOn(actor, *armor, lock, addMissing, *equipManager, piece)) {
			case Outcome::kWorn:
				result.worn.push_back(piece);
				break;
			case Outcome::kNotCarried:
				result.notCarried.push_back(armor);
				break;
			case Outcome::kRefused:
				result.refused.push_back(armor);
				break;
			}
		}
		// With no piece on, nothing is assigned: they keep what they wear.
		if (!result.worn.empty()) {
			TakeOffOthers(actor, result.worn, *equipManager);
			for (const auto& key : before) {
				const auto copy = Find(actor, key);
				const bool inOutfit = std::ranges::any_of(result.worn, [&key](const Piece& piece) { return piece.key == key; });
				if (copy.extra && !copy.extra->GetWorn() && !inOutfit) {
					result.takenOff.push_back(key);
				}
			}
		}
		if (actor.IsPlayerRef()) {
			RefreshModel(actor);
		}
		return result;
	}

	std::size_t PutBackOn(RE::Actor& actor, std::vector<Piece>& pieces)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return 0;
		}
		std::size_t count = 0;
		for (auto& piece : pieces) {
			const auto copy = Find(actor, piece.key);
			if (!copy.extra) {
				continue;
			}
			if (copy.extra->GetWorn()) {
				// Still on but no longer locked (taken off and put back by another mod, or the
				// lock suspended for the preview): the lock goes on again.
				if (!IsLocked(*copy.extra)) {
					Lock(actor, *copy.extra);
					piece.locked = true;
					++count;
				}
				continue;
			}
			if (IsLocked(*copy.extra)) {
				continue;  // an unworn locked copy cannot be equipped
			}
			equipManager->EquipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, true, false, true);
			if (const auto now = Find(actor, piece.key); now.extra && now.extra->GetWorn()) {
				piece.locked = IsLocked(*now.extra);
				++count;
			}
		}
		return count;
	}

	void Release(RE::Actor& actor, const std::vector<Piece>& pieces, bool removeCopies)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		for (const auto& piece : pieces) {
			if (const auto copy = Find(actor, piece.key); copy.extra && piece.locked) {
				copy.extra->RemoveByType(RE::ExtraDataType::kCannotWear);
				actor.AddChange(RE::TESObjectREFR::ChangeFlags::kInventory);
			}
			if (!removeCopies || !piece.added || !equipManager) {
				continue;
			}
			if (const auto copy = Find(actor, piece.key); copy.extra && copy.extra->GetWorn()) {
				equipManager->UnequipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
			}
			if (const auto copy = Find(actor, piece.key); copy.extra) {  // read again after the unequip
				actor.RemoveItem(copy.armor, 1, RE::ITEM_REMOVE_REASON::kRemove, copy.extra, nullptr);
			}
		}
		if (actor.IsPlayerRef()) {
			RefreshModel(actor);
		}
	}

	std::size_t PutBack(RE::Actor& actor, const std::vector<InstanceKey>& keys)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return 0;
		}
		std::size_t count = 0;
		for (const auto& key : keys) {
			const auto copy = Find(actor, key);
			if (!copy.extra || copy.extra->GetWorn() || IsLocked(*copy.extra)) {
				continue;
			}
			equipManager->EquipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
			if (const auto now = Find(actor, key); now.extra && now.extra->GetWorn()) {
				++count;
			}
		}
		if (actor.IsPlayerRef()) {
			RefreshModel(actor);
		}
		return count;
	}

	std::vector<RE::TESObjectARMO*> WornArmor(RE::Actor& actor)
	{
		std::vector<RE::TESObjectARMO*> worn;
		for (const auto& copy : Copies(actor)) {
			if (copy.extra->GetWorn() && IsGear(*copy.armor) && std::ranges::find(worn, copy.armor) == worn.end()) {
				worn.push_back(copy.armor);
			}
		}
		return worn;
	}
}
