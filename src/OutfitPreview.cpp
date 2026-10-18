// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   State     - the previewed actor, the pieces shown, what Begin found
//   Inventory - armor copies with extra data, read again after every native call
//   NPC       - a runtime outfit written into the base record: ShowOnNpc takes off the
//               outfit they wore (kept in the inventory) and alternates two runtime
//               forms, EndNpc puts the kept copies back on
//   Player    - inventory copies found again by ExtraUniqueID: DressPlayer picks a
//               copy per piece, RestorePlayer puts the snapshot back
//   API       - Begin / SetPiece / Show / End / Abandon / IsActive
//
// Both paths follow Tailor 3.0 (GPL-3.0), reduced to what a preview needs: the NPC
// path its src/outfit/OutfitManager.cpp (SetActorDefaultOutfit, InitializeHiddenOutfitItems,
// FlushAndApplyOutfit, BeginCreateOutfit, TryEndCreateOutfit, PrepareForGameLoad), the
// player path its src/player/PlayerWardrobe.cpp, PlayerHands.cpp and PlayerModel.cpp (Tag,
// DressItems, BeginPreview, EndPreview, ReturnHands, RefreshPlayerModel). Unlike Tailor, the
// NPC's outfit is not made again as the preview ends: that would roll its leveled lists anew.
#include "PCH.h"

#include "OutfitPreview.h"

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

	// An outfit copy an NPC wore as the preview began, found again by armor and
	// outfit mark: the copies one outfit made of a piece are alike.
	struct WornPiece
	{
		RE::FormID armor = 0;
		RE::FormID outfit = 0;  // the outfit that made the copy (its ExtraOutfitItem)
	};

	// What an NPC's base record held and what outfit they wore as the preview began;
	// End and Abandon put it back.
	struct NpcSnapshot
	{
		RE::FormID             npcID = 0;  // the base record, looked up again: the actor may be gone by then
		RE::BGSOutfit*         defaultOutfit = nullptr;
		RE::BGSOutfit*         sleepOutfit = nullptr;
		bool                   defaultHadChange = false;  // the save was recording a default outfit change
		bool                   sleepHadChange = false;
		std::vector<WornPiece> worn;
	};

	// One inventory copy of the player's, as Tailor's wardrobe identifies it: the
	// armor and the ExtraUniqueID given to its extra list. Native calls can rebuild
	// extra lists, so a copy is found again by key, never by pointer.
	struct CopyKey
	{
		RE::FormID    form = 0;
		RE::FormID    owner = 0;
		std::uint16_t unique = 0;

		friend bool operator==(const CopyKey&, const CopyKey&) = default;
	};

	// What the player wore as the preview began, and what the preview added.
	struct PlayerSnapshot
	{
		std::vector<CopyKey>        worn;
		std::vector<CopyKey>        added;    // copies the preview put in the inventory; End takes them back
		std::array<RE::TESForm*, 2> hands{};  // what the left and right hands held, armor aside: a previewed shield takes them
	};

	// Contract: game thread only. One preview at a time.
	bool                            g_active = false;
	RE::ActorHandle                 g_actor;
	bool                            g_isPlayer = false;
	std::vector<RE::TESObjectARMO*> g_pieces;
	NpcSnapshot                     g_npc;
	PlayerSnapshot                  g_player;

	// The two runtime outfits an NPC preview alternates between, created on first
	// use and kept for the whole process: the engine registers them, so dropping
	// the pointers would leak them (as Tailor notes). Emptied when a preview ends.
	std::array<RE::BGSOutfit*, 2> g_outfits{};

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

	// --- NPC ------------------------------------------------------------------

	using ChangeFlag = RE::TESNPC::ChangeFlags::ChangeFlag;

	bool HasChange(RE::TESNPC& npc, ChangeFlag flag)
	{
		auto* saveLoad = RE::BGSSaveLoadGame::GetSingleton();
		return saveLoad && saveLoad->GetChange(&npc, flag);
	}

	void SetChange(RE::TESNPC& npc, ChangeFlag flag, bool on)
	{
		if (on) {
			npc.AddChange(flag);
		} else {
			npc.RemoveChange(flag);
		}
	}

	// The outfit that made this copy: InitOutfitItems marks each copy it adds with
	// an ExtraOutfitItem naming the outfit. 0 for any other copy.
	RE::FormID OutfitMark(RE::ExtraDataList& extra)
	{
		const auto* item = extra.GetByType<RE::ExtraOutfitItem>();
		return item ? item->id : 0;
	}

	bool IsPreviewMark(RE::FormID mark)
	{
		return mark != 0 && std::ranges::any_of(g_outfits, [mark](const RE::BGSOutfit* outfit) { return outfit && outfit->GetFormID() == mark; });
	}

	// The outfit copies they wear. Their own armor is left to the capture window,
	// which gives it back after the close.
	std::vector<WornPiece> WornOutfitPieces(RE::Actor& actor)
	{
		std::vector<WornPiece> worn;
		for (const auto& copy : Copies(actor)) {
			if (const auto mark = OutfitMark(*copy.extra); mark != 0 && copy.extra->GetWorn()) {
				worn.push_back({ copy.armor->GetFormID(), mark });
			}
		}
		return worn;
	}

	// Writes both outfits into the base record. A runtime outfit's 0xFF FormID does
	// not survive a load, so the change bits are cleared; End and Abandon put the snapshot's back.
	void WriteOutfits(RE::TESNPC& npc, RE::BGSOutfit* outfit, RE::BGSOutfit* sleepOutfit)
	{
		npc.SetDefaultOutfit(outfit);
		npc.SetSleepOutfit(sleepOutfit);
		npc.RemoveChange(ChangeFlag::kDefaultOutfit);
		npc.RemoveChange(ChangeFlag::kSleepOutfit);
	}

	// Takes every copy a preview outfit made out of the inventory, worn ones off first.
	// Contract: game thread. Nothing else is removed: the NPC's own copies and the
	// copies of the outfit they wore stay, and End puts the outfit's back on.
	void RemovePreviewCopies(RE::Actor& actor, RE::ActorEquipManager& equipManager)
	{
		const auto preview = [](const Copy& copy) { return IsPreviewMark(OutfitMark(*copy.extra)); };
		const auto worn = [&preview](const Copy& copy) { return preview(copy) && copy.extra->GetWorn(); };
		ForEachCopy(actor, worn, [&](const Copy& copy) {
			equipManager.UnequipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
		});
		ForEachCopy(actor, preview, [&](const Copy& copy) {
			actor.RemoveItem(copy.armor, copy.extra->GetCount(), RE::ITEM_REMOVE_REASON::kRemove, copy.extra, nullptr);
		});
	}

	// Takes off the outfit copies they wear that no preview outfit made. The copies
	// stay in the inventory for End.
	void TakeOffOtherOutfits(RE::Actor& actor, RE::ActorEquipManager& equipManager)
	{
		const auto other = [](const Copy& copy) {
			const auto mark = OutfitMark(*copy.extra);
			return mark != 0 && !IsPreviewMark(mark) && copy.extra->GetWorn();
		};
		ForEachCopy(actor, other, [&](const Copy& copy) {
			equipManager.UnequipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
		});
	}

	// Adds the outfit's copies and puts each on; leveled lists in it are rolled now.
	// Tailor's InitializeHiddenOutfitItems.
	void DressInOutfit(RE::Actor& actor, RE::BGSOutfit& outfit, RE::ActorEquipManager& equipManager)
	{
		if (auto* changes = actor.GetInventoryChanges()) {
			changes->InitOutfitItems(&outfit, actor.GetLevel());
		}
		if (actor.IsDisabled()) {
			return;
		}
		for (const auto& copy : Copies(actor)) {  // collected before any equip, which can change the inventory
			if (OutfitMark(*copy.extra) == outfit.GetFormID()) {
				equipManager.EquipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
			}
		}
	}

	// True when they wore outfit copies as the preview began and none of those is
	// left: Tailor or the engine dressed them anew while the window was open.
	bool OutfitCopiesGone(RE::Actor& actor)
	{
		const auto copies = Copies(actor);
		return !g_npc.worn.empty() && std::ranges::none_of(g_npc.worn, [&copies](const WornPiece& piece) {
			return std::ranges::any_of(copies, [&piece](const Copy& copy) { return OutfitMark(*copy.extra) == piece.outfit; });
		});
	}

	// Puts each outfit copy they wore as the preview began back on, so pieces the
	// game rolled from leveled lists stay as rolled. Returns how many they wear again.
	std::size_t PutBackWorn(RE::Actor& actor, RE::ActorEquipManager& equipManager)
	{
		std::size_t on = 0;
		for (const auto& piece : g_npc.worn) {
			auto*      armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(piece.armor);
			const auto copies = armor ? Copies(actor, armor) : std::vector<Copy>{};
			const auto same = [&piece](const Copy& copy) { return OutfitMark(*copy.extra) == piece.outfit; };
			if (std::ranges::any_of(copies, [&same](const Copy& copy) { return same(copy) && copy.extra->GetWorn(); })) {
				++on;  // still on
			} else if (const auto kept = std::ranges::find_if(copies, same); kept != copies.end()) {
				equipManager.EquipObject(&actor, armor, kept->extra, 1, nullptr, false, false, false, true);
				++on;
			} else {
				logger::warn("Preview could not put armor {:08X} back on {:08X}: its copy is gone", piece.armor, actor.GetFormID());
			}
		}
		return on;
	}

	// Snapshots the base record and the outfit they wear. Returns "" when the NPC
	// can be previewed, or why not.
	std::string BeginNpc(RE::Actor& actor, RE::TESNPC& npc)
	{
		for (auto& outfit : g_outfits) {
			if (!outfit) {
				auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSOutfit>();
				outfit = factory ? factory->Create() : nullptr;
				if (!outfit) {
					return "Preview off: the preview outfit could not be created.";
				}
				logger::info("Created preview outfit {:08X}", outfit->GetFormID());
			}
		}
		g_npc = { npc.GetFormID(), npc.defaultOutfit, npc.sleepOutfit, HasChange(npc, ChangeFlag::kDefaultOutfit), HasChange(npc, ChangeFlag::kSleepOutfit),
			WornOutfitPieces(actor) };
		npc.RemoveChange(ChangeFlag::kDefaultOutfit);
		npc.RemoveChange(ChangeFlag::kSleepOutfit);
		return {};
	}

	// Dresses the NPC in g_pieces through the runtime outfit they do not wear now,
	// so the engine always sees a new outfit (Tailor's double buffer). The sleep
	// outfit is the same form, so a vanilla sleep outfit cannot take body slots
	// while they are in bed. The outfit they wore comes off and stays in the inventory.
	// Contract: game thread.
	void ShowOnNpc(RE::Actor& actor, RE::TESNPC& npc)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return;
		}
		auto* outfit = npc.defaultOutfit == g_outfits[0] ? g_outfits[1] : g_outfits[0];
		outfit->outfitItems.clear();
		for (auto* armor : g_pieces) {
			outfit->outfitItems.push_back(armor);
		}
		actor.InitInventoryIfRequired();
		RemovePreviewCopies(actor, *equipManager);
		WriteOutfits(npc, outfit, outfit);
		TakeOffOtherOutfits(actor, *equipManager);
		DressInOutfit(actor, *outfit, *equipManager);
	}

	// Puts the base record back and the outfit copies they wore back on. The outfit
	// is made again only when those copies are gone, which rolls its leveled lists anew.
	// Contract: game thread.
	void EndNpc(RE::Actor& actor, RE::TESNPC& npc, RE::ActorEquipManager& equipManager)
	{
		RemovePreviewCopies(actor, equipManager);
		WriteOutfits(npc, g_npc.defaultOutfit, g_npc.sleepOutfit);
		if (g_npc.defaultOutfit && OutfitCopiesGone(actor)) {
			logger::info("The outfit {:08X} wore was replaced during the preview; outfit {:08X} goes on again", actor.GetFormID(), g_npc.defaultOutfit->GetFormID());
			DressInOutfit(actor, *g_npc.defaultOutfit, equipManager);
		}
		if (!actor.IsDisabled()) {
			const auto on = PutBackWorn(actor, equipManager);
			logger::info("Put {} of {} outfit piece(s) back on {:08X}", on, g_npc.worn.size(), actor.GetFormID());
		}
	}

	void RestoreNpcChanges(RE::TESNPC& npc)
	{
		SetChange(npc, ChangeFlag::kDefaultOutfit, g_npc.defaultHadChange);
		SetChange(npc, ChangeFlag::kSleepOutfit, g_npc.sleepHadChange);
	}

	void ClearOutfits()
	{
		for (auto* outfit : g_outfits) {
			if (outfit) {
				outfit->outfitItems.clear();
			}
		}
	}

	// --- Player ---------------------------------------------------------------

	// Non-playable armor (skin, body-mod parts) is never the player's gear.
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

	void Equip(RE::Actor& actor, const CopyKey& key)
	{
		const auto copy = Find(actor, key);
		if (copy.extra && !copy.extra->GetWorn()) {
			RE::ActorEquipManager::GetSingleton()->EquipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
		}
	}

	void Unequip(RE::Actor& actor, const CopyKey& key)
	{
		const auto copy = Find(actor, key);
		if (copy.extra && copy.extra->GetWorn()) {
			RE::ActorEquipManager::GetSingleton()->UnequipObject(&actor, copy.armor, copy.extra, 1, nullptr, false, false, false, true);
		}
	}

	// Removes exactly this copy, worn or not; one already gone is simply gone.
	void TakeBack(RE::Actor& actor, const CopyKey& key)
	{
		Unequip(actor, key);
		if (const auto copy = Find(actor, key); copy.armor) {
			actor.RemoveItem(copy.armor, 1, RE::ITEM_REMOVE_REASON::kRemove, copy.extra, nullptr);
		}
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

	// Dresses the player in g_pieces: each piece through a copy already worn, else
	// the one worn as the preview began, else a copy the preview added, else any
	// copy they own, else a copy the preview adds now. Everything else worn comes
	// off, and preview copies the look no longer uses go back. Tailor's DressItems.
	// Contract: game thread; the player's own copies are never removed.
	void DressPlayer(RE::Actor& player)
	{
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return;
		}
		const auto                                       worn = WornGear(player);
		const std::array<const std::vector<CopyKey>*, 3> known{ &worn, &g_player.worn, &g_player.added };
		std::vector<CopyKey>                             chosen;
		for (auto* armor : g_pieces) {
			const auto form = armor->GetFormID();
			if (std::ranges::any_of(chosen, [form](const CopyKey& key) { return key.form == form; })) {
				continue;
			}
			std::optional<CopyKey> pick;
			for (const auto* keys : known) {
				for (const auto& key : *keys) {
					if (key.form == form && Find(player, key).armor) {
						pick = key;
						break;
					}
				}
				if (pick) {
					break;
				}
			}
			if (pick) {
				Equip(player, *pick);
			} else if (CountOf(player, *armor) > 0) {
				// Plain copies carry no extra data: the equip manager picks one.
				equipManager->EquipObject(&player, armor, nullptr, 1, nullptr, false, false, false, true);
				pick = TagWorn(player, *armor);
			} else {
				player.AddObjectToContainer(armor, nullptr, 1, nullptr);
				equipManager->EquipObject(&player, armor, nullptr, 1, nullptr, false, false, false, true);
				pick = TagWorn(player, *armor);
				if (pick) {
					g_player.added.push_back(*pick);
				} else {
					player.RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);  // refused (race, a locked slot): the copy goes straight back
				}
			}
			if (pick) {
				chosen.push_back(*pick);
			} else {
				logger::warn("Preview could not put armor {:08X} on the player", form);
			}
		}
		for (const auto& key : worn) {
			if (!Contains(chosen, key)) {
				Unequip(player, key);
			}
		}
		std::vector<CopyKey> kept;
		for (const auto& key : g_player.added) {
			if (Contains(chosen, key)) {
				kept.push_back(key);
			} else {
				TakeBack(player, key);
			}
		}
		g_player.added = std::move(kept);
	}

	// Puts back exactly what the player wore as the preview began. Their own copies
	// were never removed and the same extra lists go on again, so tempering,
	// enchantments and names keep.
	void RestorePlayer(RE::Actor& player)
	{
		for (const auto& key : WornGear(player)) {
			if (!Contains(g_player.worn, key)) {
				Unequip(player, key);
			}
		}
		for (const auto& key : g_player.added) {
			TakeBack(player, key);
		}
		for (const auto& key : g_player.worn) {
			Equip(player, key);  // nothing for a copy that is gone
		}
		ReturnHand(player, true, g_player.hands[0]);
		ReturnHand(player, false, g_player.hands[1]);
	}

	// --- API ------------------------------------------------------------------

	// Rebuilds the character's model from what they wear now. Equip calls alone can
	// leave it behind: the player's lags behind the ticks, and an NPC whose outfit
	// came off just before the same pieces went on shows bare. Tailor's
	// RefreshPlayerModel. CommonLibSSE-NG's Update3DModel also sends SKSE's
	// NiNodeUpdate event, so body-morph mods refit the new pieces.
	void RefreshModel(RE::Actor& actor)
	{
		auto* process = actor.Is3DLoaded() ? actor.GetActorRuntimeData().currentProcess : nullptr;
		if (!process) {
			return;
		}
		process->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kModel);
		process->Update3DModel(&actor);
	}

	void Redress(RE::Actor& actor)
	{
		if (g_isPlayer) {
			DressPlayer(actor);
		} else if (auto* npc = actor.GetActorBase()) {
			ShowOnNpc(actor, *npc);
		}
		RefreshModel(actor);
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
		ClearOutfits();
		g_pieces.clear();
		g_npc = {};
		g_player = {};
	}
}

namespace OutfitPreview
{
	std::string Begin(RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& pieces)
	{
		End();  // a preview still on goes back first
		g_isPlayer = actor.IsPlayerRef();
		if (g_isPlayer) {
			g_player = { WornGear(actor), {}, { HeldIn(actor, true), HeldIn(actor, false) } };
		} else {
			auto* npc = actor.GetActorBase();
			if (!npc) {
				return "Preview off: the character has no base record.";
			}
			if (auto reason = BeginNpc(actor, *npc); !reason.empty()) {
				return reason;
			}
		}
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
		if (g_isPlayer) {
			if (actor) {
				RestorePlayer(*actor);
				RefreshModel(*actor);
			}
		} else if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(g_npc.npcID)) {
			// The actor may be gone (unloaded); the record is put right either way, or their
			// next load would dress them in an emptied preview outfit.
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (actor && equipManager) {
				EndNpc(*actor, *npc, *equipManager);
			} else {
				npc->defaultOutfit = g_npc.defaultOutfit;
				npc->sleepOutfit = g_npc.sleepOutfit;
			}
			RestoreNpcChanges(*npc);
		}
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
		if (!g_isPlayer) {
			if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(g_npc.npcID)) {
				npc->defaultOutfit = g_npc.defaultOutfit;
				npc->sleepOutfit = g_npc.sleepOutfit;
				RestoreNpcChanges(*npc);
			}
		}
		ClearState();
	}

	bool IsActive()
	{
		return g_active;
	}
}
