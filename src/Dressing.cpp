// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Inventory - armor copies with extra data, found again by ExtraUniqueID; the lock; the
//               outfit-item mark for the trade-menu filter
//   Equip     - choose an instance per piece, put it on, take off the rest; choose the
//               player's copy to give away
//   API       - RefreshModel / Dress / PutBackOn / TakeOffExtras / Replenish / Upgrade / Release /
//               PutBack / MarkAsOutfitItems / UnmarkOutfitItems / WornArmor / MoveFromPlayer
//
// The inventory helpers follow src/OutfitPreview.cpp, itself from Tailor 3.0's
// src/player/PlayerWardrobe.cpp; setting ExtraCannotWear directly follows Tailor's
// src/wig/WigEquipment.cpp. docs/design.md §6 lists what was verified in game.
#include "PCH.h"

#include "Dressing.h"

#include "Settings.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

namespace
{
	// --- Constants ------------------------------------------------------------

	// Headgear for [General] UnusedSlots: helmets, hoods and circlets each take one of these
	// (body slots 30, 31, 41, 42).
	using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
	constexpr auto kHeadgearSlots = std::to_underlying(Slot::kHead) | std::to_underlying(Slot::kHair) | std::to_underlying(Slot::kLongHair) |
	                                std::to_underlying(Slot::kCirclet);

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

	// An ExtraOutfitItem naming `outfit`. CommonLibSSE-NG declares the class without a
	// constructor, so it is built the way the library's own extra-data constructors build one:
	// on the engine's heap, where ExtraDataList::RemoveByType frees it, with the engine's vtable.
	RE::ExtraOutfitItem* MakeOutfitItemMark(RE::FormID outfit)
	{
		auto* mark = RE::malloc<RE::ExtraOutfitItem>();
		if (!mark) {
			return nullptr;
		}
		std::memset(static_cast<void*>(mark), 0, sizeof(RE::ExtraOutfitItem));  // next = nullptr
		if (!SKSE::stl::emplace_vtable(mark)) {
			RE::free(mark);
			return nullptr;
		}
		mark->id = outfit;
		return mark;
	}

	// --- Equip ----------------------------------------------------------------

	// An instance to dress them in; a plain copy has no extra list, and the equip
	// manager picks one of those itself.
	struct Instance
	{
		RE::ExtraDataList* extra = nullptr;  // nullptr: a plain copy
	};

	// Enchanted first, then the most tempered; a higher rank is the better copy.
	using Rank = std::pair<bool, float>;

	Rank RankOf(const RE::ExtraDataList& extra)
	{
		const auto* health = extra.GetByType<RE::ExtraHealth>();
		return { extra.HasType<RE::ExtraEnchantment>(), health ? health->health : 1.0f };  // 1 = not tempered
	}

	bool IsWellMade(const Rank& rank)
	{
		return rank.first || rank.second > 1.0f;
	}

	// Whether they carry an unworn copy that could go on and outranks `worn`. Equal ranks are
	// no reason to swap, so a swap settles: the copy it puts on is then the best one.
	bool HasBetterCopy(RE::Actor& actor, const RE::TESObjectARMO& armor, const RE::ExtraDataList& worn)
	{
		const auto rank = RankOf(worn);
		return std::ranges::any_of(Copies(actor, &armor), [&rank](const Copy& copy) {
			return !copy.extra->GetWorn() && !IsLocked(*copy.extra) && RankOf(*copy.extra) > rank;
		});
	}

	// Ranked as docs/design.md §2 says: enchanted, most tempered, worn, plain; any other copy
	// last. The worn copy stays when no unworn one outranks it, or when it is locked: Wear
	// releases Livery's locks before it dresses, so a lock here is another mod's. nullopt when
	// they carry none. An unworn locked copy cannot be equipped and is skipped.
	std::optional<Instance> PickInstance(RE::Actor& actor, RE::TESObjectARMO& armor)
	{
		if (const auto worn = WornCopy(actor, armor); worn.extra && (IsLocked(*worn.extra) || !HasBetterCopy(actor, armor, *worn.extra))) {
			return Instance{ worn.extra };
		}
		RE::ExtraDataList* best = nullptr;
		Rank               bestRank;
		std::int32_t       listed = 0;  // copies on extra lists, skipped ones included
		for (const auto& copy : Copies(actor, &armor)) {
			listed += copy.extra->GetCount();
			if (copy.extra->GetWorn() || IsLocked(*copy.extra)) {
				continue;
			}
			if (const auto rank = RankOf(*copy.extra); !best || rank > bestRank) {
				best = copy.extra;
				bestRank = rank;
			}
		}
		if (best && IsWellMade(bestRank)) {
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

	// The player's copy of `armor` to give away, ranked as PickInstance ranks the unworn ones
	// (enchanted, most tempered, plain); the worn copy only with `allowWorn` and no other.
	// An unworn locked copy is skipped: the NPC could not put it on. nullopt for none.
	std::optional<Instance> PickCopyToGive(RE::Actor& player, RE::TESObjectARMO& armor, bool allowWorn)
	{
		RE::ExtraDataList* best = nullptr;
		RE::ExtraDataList* worn = nullptr;
		Rank               bestRank;
		std::int32_t       listed = 0;  // copies on extra lists, skipped ones included
		for (const auto& copy : Copies(player, &armor)) {
			listed += copy.extra->GetCount();
			if (copy.extra->GetWorn()) {
				worn = copy.extra;
				continue;
			}
			if (IsLocked(*copy.extra)) {
				continue;
			}
			if (const auto rank = RankOf(*copy.extra); !best || rank > bestRank) {
				best = copy.extra;
				bestRank = rank;
			}
		}
		if (best && IsWellMade(bestRank)) {
			return Instance{ best };
		}
		if (CountOf(player, armor) > listed) {
			return Instance{};  // a plain copy, which has no extra list
		}
		if (best) {
			return Instance{ best };
		}
		if (worn && allowWorn) {
			return Instance{ worn };
		}
		return std::nullopt;
	}

	enum class Outcome
	{
		kWorn,
		kNotCarried,
		kRefused
	};

	// Takes the worn copy off and puts the best copy on in its place; when that does not go on,
	// the worn copy goes back on. Taken off first so no equip call depends on what the engine
	// does with two copies of one armor; the pick is read again because the unequip can
	// rebuild the extra lists. Returns whether the better copy is on: WornCopy then finds it.
	// Contract: `worn` is unlocked, and HasBetterCopy said yes.
	bool SwapForBetter(RE::Actor& actor, RE::TESObjectARMO& armor, const Copy& worn, bool lock, RE::ActorEquipManager& equipManager)
	{
		const auto wornKey = Tag(actor, worn);
		equipManager.UnequipObject(&actor, &armor, worn.extra, 1, nullptr, false, false, false, true);
		if (WornCopy(actor, armor).extra) {
			return false;  // it would not come off
		}
		if (const auto better = PickInstance(actor, armor)) {
			equipManager.EquipObject(&actor, &armor, better->extra, 1, nullptr, false, lock, false, true);
			if (WornCopy(actor, armor).extra) {
				return true;
			}
		}
		if (const auto back = Find(actor, wornKey); back.extra) {
			equipManager.EquipObject(&actor, &armor, back.extra, 1, nullptr, false, lock, false, true);
		}
		return false;
	}

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
		// A worn copy here was not picked, so a better one was: PickInstance keeps a locked one.
		if (const auto current = WornCopy(actor, armor); current.extra) {
			SwapForBetter(actor, armor, current, lock, equipManager);
		} else {
			// forceEquip is PreventRemoval: the engine puts ExtraCannotWear on the copy it equips.
			equipManager.EquipObject(&actor, &armor, instance->extra, 1, nullptr, false, lock, false, true);
		}
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

	// Whether armor that is none of the pieces and no outfit item may stay on in these body slots
	// (Dressing::TakeOffExtras): never over a worn piece (`worn`, their slots); in a slot no piece
	// has (`outfit`, the slots of every piece, worn or gone) as [General] UnusedSlots says; only in
	// the slots of pieces gone it stays, those slots being the engine's until the next dressing
	// (docs/design.md §2).
	bool MayStay(std::uint32_t slots, std::uint32_t worn, std::uint32_t outfit, Settings::UnusedSlots mode)
	{
		if ((slots & worn) != 0) {
			return false;
		}
		if ((slots & ~outfit) == 0) {
			return true;
		}
		const bool unusedOff = mode == Settings::UnusedSlots::kOutfitOnly ||
		                       (mode == Settings::UnusedSlots::kExceptHeadgear && (slots & kHeadgearSlots) != 0);
		return !unusedOff;
	}

	// The copy of `armor` they could put on in place of an outfit item: of the copies they carry
	// that are unworn, unlocked and no outfit item, the best as PickInstance ranks the unworn ones.
	// nullopt for none.
	std::optional<Instance> VisibleCopy(RE::Actor& actor, RE::TESObjectARMO& armor)
	{
		RE::ExtraDataList* best = nullptr;
		Rank               bestRank;
		std::int32_t       listed = 0;  // copies on extra lists, skipped ones included
		for (const auto& copy : Copies(actor, &armor)) {
			listed += copy.extra->GetCount();
			if (copy.extra->GetWorn() || IsLocked(*copy.extra) || copy.extra->HasType<RE::ExtraOutfitItem>()) {
				continue;
			}
			if (const auto rank = RankOf(*copy.extra); !best || rank > bestRank) {
				best = copy.extra;
				bestRank = rank;
			}
		}
		const bool plain = CountOf(actor, armor) > listed;
		if (!best && !plain) {
			return std::nullopt;
		}
		return Instance{ best && (IsWellMade(bestRank) || !plain) ? best : nullptr };  // nullptr: a plain copy
	}

	// The armor that goes on in place of outfit items taken off from body slots `freed`: of the
	// playable armor they carry with a copy VisibleCopy finds, in slots that overlap `freed` and
	// where other armor may stay (MayStay), the highest base armor rating, then the highest value.
	// nullptr for none.
	RE::TESObjectARMO* BestInstead(RE::Actor& actor, std::uint32_t freed, std::uint32_t worn, std::uint32_t outfit, Settings::UnusedSlots mode)
	{
		const auto outranks = [](const RE::TESObjectARMO& a, const RE::TESObjectARMO& b) {
			return a.armorRating != b.armorRating ? a.armorRating > b.armorRating : a.GetGoldValue() > b.GetGoldValue();
		};
		RE::TESObjectARMO* best = nullptr;
		const auto         inventory = actor.GetInventory([](RE::TESBoundObject& item) { return item.IsArmor(); });
		for (const auto& [object, data] : inventory) {
			auto*      armor = object->As<RE::TESObjectARMO>();
			const auto slots = armor ? armor->GetSlotMask().underlying() : 0u;
			if (!armor || data.first <= 0 || !IsGear(*armor) || (slots & freed) == 0 || !MayStay(slots, worn, outfit, mode)) {
				continue;
			}
			if ((!best || outranks(*armor, *best)) && VisibleCopy(actor, *armor)) {
				best = armor;
			}
		}
		return best;
	}

	// Puts on, unlocked, in place of outfit items taken off from body slots `freed`, the armor
	// BestInstead picks, its copy VisibleCopy picks: the worn-gear pass's best-armor step puts an
	// outfit item on over armor they were given that is the same armor or weaker (docs/design.md
	// §2). Which copy the engine equips for a plain one (no extra list) is not known, so the copy
	// worn is checked: an outfit copy comes off again. Logs the outcome; it runs only after an
	// outfit item came off, never in normal play.
	void PutOnBestInstead(RE::Actor& actor, std::uint32_t freed, std::uint32_t worn, std::uint32_t outfit, Settings::UnusedSlots mode,
		RE::ActorEquipManager& equipManager)
	{
		auto*                   armor = BestInstead(actor, freed, worn, outfit, mode);
		std::optional<Instance> copy;
		if (armor) {
			copy = VisibleCopy(actor, *armor);
		}
		if (!copy) {
			logger::info("Nothing they carry goes on {:08X} in place of their outfit item", actor.GetFormID());
			return;
		}
		equipManager.EquipObject(&actor, armor, copy->extra, 1, nullptr, false, false, false, true);
		const auto  wornCopy = WornCopy(actor, *armor);
		const auto* name = armor->GetName();
		if (!wornCopy.extra) {
			logger::info("{} ({:08X}) did not go on {:08X} in place of their outfit item", name, armor->GetFormID(), actor.GetFormID());
		} else if (wornCopy.extra->HasType<RE::ExtraOutfitItem>()) {
			equipManager.UnequipObject(&actor, armor, wornCopy.extra, 1, nullptr, false, false, false, true);
			logger::info("The game put the outfit copy of {} ({:08X}) on {:08X} again; taken off, the slot stays empty", name, armor->GetFormID(),
				actor.GetFormID());
		} else {
			logger::info("Put {} of {} ({:08X}) on {:08X} in place of their outfit item", copy->extra ? "a copy with extra data" : "a plain copy", name,
				armor->GetFormID(), actor.GetFormID());
		}
	}
}

namespace Dressing
{
	// Tailor's RefreshPlayerModel, as in OutfitPreview.cpp.
	void RefreshModel(RE::Actor& actor)
	{
		auto* process = actor.Is3DLoaded() ? actor.GetActorRuntimeData().currentProcess : nullptr;
		if (!process) {
			return;
		}
		process->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kModel);
		process->Update3DModel(&actor);
	}

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
				// By armor: a copy a better one replaced is no piece of theirs to put back on.
				const bool inOutfit = std::ranges::any_of(result.worn, [&key](const Piece& piece) { return piece.key.armor == key.armor; });
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

	std::size_t TakeOffExtras(RE::Actor& actor, const std::vector<Piece>& pieces)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager || pieces.empty()) {
			return 0;  // no pieces: they wear their own clothes, every slot the engine's
		}
		std::uint32_t worn = 0;    // the body slots of the pieces they wear
		std::uint32_t outfit = 0;  // the body slots of every piece, worn or gone
		for (const auto& piece : pieces) {
			if (const auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(piece.key.armor)) {
				outfit |= armor->GetSlotMask().underlying();
			}
			if (const auto copy = Find(actor, piece.key); copy.extra && copy.extra->GetWorn()) {
				worn |= copy.armor->GetSlotMask().underlying();
			}
		}
		const auto mode = Settings::UnusedSlotsMode();
		const auto extra = [&pieces, worn, outfit, mode](const Copy& copy) {
			if (!copy.extra->GetWorn() || !IsGear(*copy.armor) || IsLocked(*copy.extra)) {
				return false;
			}
			// An outfit item (any ExtraOutfitItem: their own outfit, or another mod's distributed
			// one), which the trade menu hides, always comes off; other armor as MayStay says.
			if (!copy.extra->HasType<RE::ExtraOutfitItem>() && MayStay(copy.armor->GetSlotMask().underlying(), worn, outfit, mode)) {
				return false;
			}
			const auto key = KeyOf(copy);
			return !key || std::ranges::none_of(pieces, [&key](const Piece& piece) { return piece.key == *key; });
		};
		const auto countExtras = [&] { return std::ranges::count_if(Copies(actor), extra); };
		const auto before = countExtras();
		if (before == 0) {
			return 0;
		}
		std::vector<std::uint32_t> freed;  // the body slots of each outfit item taken off where other armor may stay
		ForEachCopy(actor, extra, [&](const Copy& copy) {
			const auto slots = copy.armor->GetSlotMask().underlying();
			if (copy.extra->HasType<RE::ExtraOutfitItem>() && MayStay(slots, worn, outfit, mode) && std::ranges::find(freed, slots) == freed.end()) {
				freed.push_back(slots);
			}
			equipManager->UnequipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
		});
		const auto after = countExtras();
		for (const auto slots : freed) {
			PutOnBestInstead(actor, slots, worn, outfit, mode, *equipManager);
		}
		return before > after ? static_cast<std::size_t>(before - after) : 0;
	}

	std::size_t Replenish(RE::Actor& actor, std::vector<Piece>& pieces)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return 0;
		}
		std::size_t count = 0;
		for (auto& piece : pieces) {
			if (piece.added || Find(actor, piece.key).extra) {
				continue;
			}
			auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(piece.key.armor);
			if (armor && PutOn(actor, *armor, true, true, *equipManager, piece) == Outcome::kWorn) {
				++count;
			}
		}
		return count;
	}

	std::size_t Upgrade(RE::Actor& actor, std::vector<Piece>& pieces)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return 0;
		}
		std::size_t count = 0;
		for (auto& piece : pieces) {
			const auto worn = Find(actor, piece.key);
			// A piece not worn is PutBackOn's; a lock this module did not set is another mod's.
			if (!worn.extra || !worn.extra->GetWorn() || (IsLocked(*worn.extra) && !piece.locked) || !HasBetterCopy(actor, *worn.armor, *worn.extra)) {
				continue;
			}
			if (piece.locked) {
				worn.extra->RemoveByType(RE::ExtraDataType::kCannotWear);
				actor.AddChange(RE::TESObjectREFR::ChangeFlags::kInventory);
			}
			const auto oldKey = piece.key;
			if (!SwapForBetter(actor, *worn.armor, worn, true, *equipManager)) {
				const auto old = Find(actor, oldKey);
				const bool oldWorn = old.extra && old.extra->GetWorn();
				if (oldWorn && !IsLocked(*old.extra)) {
					Lock(actor, *old.extra);
				}
				piece.locked = oldWorn;
				continue;
			}
			const auto now = WornCopy(actor, *worn.armor);
			const bool wasAdded = piece.added;
			piece = { Tag(actor, now), false, IsLocked(*now.extra) };  // a better copy is never one this module added
			if (const auto old = Find(actor, oldKey); wasAdded && old.extra) {
				actor.RemoveItem(old.armor, 1, RE::ITEM_REMOVE_REASON::kRemove, old.extra, nullptr);
			}
			++count;
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

	std::vector<InstanceKey> MarkAsOutfitItems(RE::Actor& actor, const std::vector<Piece>& pieces, RE::FormID outfit)
	{
		std::vector<InstanceKey> marked;
		for (const auto& piece : pieces) {
			if (!piece.added) {
				continue;
			}
			const auto copy = Find(actor, piece.key);
			if (!copy.extra || copy.extra->HasType<RE::ExtraOutfitItem>()) {
				continue;
			}
			if (auto* mark = MakeOutfitItemMark(outfit)) {
				copy.extra->Add(mark);
				marked.push_back(piece.key);
			}
		}
		return marked;
	}

	void UnmarkOutfitItems(RE::Actor& actor, const std::vector<InstanceKey>& keys)
	{
		for (const auto& key : keys) {
			if (const auto copy = Find(actor, key); copy.extra) {
				copy.extra->RemoveByType(RE::ExtraDataType::kOutfitItem);
			}
		}
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

	std::vector<RE::TESObjectARMO*> MoveFromPlayer(RE::Actor& player, RE::Actor& npc, const std::vector<RE::TESObjectARMO*>& armor, bool allowWorn)
	{
		std::vector<RE::TESObjectARMO*> notMoved;
		for (auto* piece : armor) {
			if (!piece) {
				continue;
			}
			const auto before = CountOf(npc, *piece);
			// The extra list is used at once: the move rebuilds the player's lists.
			if (const auto instance = PickCopyToGive(player, *piece, allowWorn)) {
				player.RemoveItem(piece, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, instance->extra, &npc);
			}
			if (CountOf(npc, *piece) <= before) {
				notMoved.push_back(piece);
			}
		}
		return notMoved;
	}
}
