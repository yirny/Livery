// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Library writes - SaveOutfit adds the outfit to Livery's library (Library); UpdateOutfit
//                    replaces a confirmed existing outfit's pieces and name;
//                    UpdateWornPieces only its pieces (the Apply popup's Yes)
//   Preview        - the captured actor wears the ticked pieces, or the outfit selected on the
//                    outfit pages, through OutfitPreview, seen through PreviewCamera, while the
//                    window's Live Preview tick is on; saving is off and an NPC is held where they
//                    stand (MovementHold) while the window is open, and turns to the player with a
//                    greeting (NpcReaction) as it opens with the preview on; every close of the window,
//                    a save included, ends them all
//   Capture        - hotkey handler: check the target (the player when nothing is aimed at), collect inventory
//                    armor (and for an NPC the player's, per the settings), each piece with its group,
//                    SkyUI's Type and Class (ArmorInfo) and its icon (PieceIcons), detect the class, list the library's outfits (adding
//                    their pieces nobody carries), then open the window with the worn pieces ticked; the window's Save
//                    writes the library, then Create and Apply gives the outfit a row (Assignments::Change)
//                    and Update dresses the actor again when they wear it; Apply moves
//                    the ticked pieces from the player's inventory to an NPC (Dressing::MoveFromPlayer)
//                    and dresses them (Assignments) with the window open (DressCaptured), as
//                    Manage Wardrobe's Apply does; Restore goes to Assignments;
//                    Manage Wardrobe' Update writes the library and redresses the actor when they
//                    wear the outfit; its Remove writes the library, clears the outfit from the
//                    assignments (Assignments::ForgetOutfit) and takes those wearing it for a
//                    situation out of it: the captured actor at once, the others at the
//                    window's close or, out of reach, when they load; both hand the window the
//                    rebuilt list (CaptureWindow::SetOutfits), with the captured actor's pieces
//                    as they are after a redress;
//                    Manage Situations' Save Changes writes the situations' names
//                    (SituationStore), then the captured actor's rows
//                    (Assignments::SetRows) and redresses them as Update does; Remove
//                    Situation writes situations.json, clears the situation's rows
//                    (Assignments::ForgetSituation) and takes those wearing it out of it as
//                    Remove does; Restore Presets writes situations.json; the settings page's
//                    Release everyone goes to Assignments
//   Event sinks    - crosshair target, the capture hotkey
//   Messaging      - situation data, outfit library and piece icon rules, settings page, sink and menu registration,
//                    ready state, the situation poll (Situations) around loads
//   Entry          - log, settings, co-save, trampoline, hooks (overlay, trade-menu filter)
#include "PCH.h"

#include "ArmorInfo.h"
#include "Assignments.h"
#include "CaptureWindow.h"
#include "Dressing.h"
#include "Hooks.h"
#include "Library.h"
#include "MovementHold.h"
#include "NpcReaction.h"
#include "OutfitPreview.h"
#include "PieceIcons.h"
#include "PreviewCamera.h"
#include "Settings.h"
#include "SituationStore.h"
#include "Situations.h"
#include "TradeFilter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	RE::ObjectRefHandle g_target{};
	bool                g_ready = false;

	void Notify(const std::string& text)
	{
		RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		logger::info("{}", text);
	}

	// --- Library writes -------------------------------------------------------

	// First "New Outfit NNN" the library does not have yet, or "" when it
	// has all of them.
	std::string NextAutomaticName()
	{
		for (int index = 1; index <= 99999; ++index) {
			auto name = std::format("New Outfit {:03}", index);
			if (!Library::FindByName(name)) {
				return name;
			}
		}
		return {};
	}

	// Request::categories: Default and the situations not removed (SituationStore::Offered)
	// by their names, one per row of Request::assigned (AttachSave).
	std::vector<CaptureWindow::CategoryOption> CategoryOptions()
	{
		std::vector<CaptureWindow::CategoryOption> categories;
		categories.push_back({ CaptureWindow::kSituationGroup, SituationStore::Name(Situations::Situation::kDefault) });
		for (const auto situation : SituationStore::Offered()) {
			categories.push_back({ CaptureWindow::kSituationGroup, SituationStore::Name(situation) });
		}
		return categories;
	}

	// Contract: game thread. Library::Create checks every refusal before it changes
	// anything, so a refusal never leaves an empty outfit.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> SaveOutfit(const std::string& name, const std::vector<RE::TESObjectARMO*>& armors, const OutfitIcons::Icon& icon)
	{
		if (auto error = Library::Create(name, armors, icon)) {
			return error;
		}
		Notify(std::format("Livery: saved {} ({} items).", name, armors.size()));
		return std::nullopt;
	}

	std::vector<RE::TESObjectARMO*> PickPieces(const std::vector<RE::TESObjectARMO*>& armors, const std::vector<std::size_t>& indices)
	{
		std::vector<RE::TESObjectARMO*> picked;
		picked.reserve(indices.size());
		for (const auto index : indices) {
			picked.push_back(armors[index]);
		}
		return picked;
	}

	// Contract: game thread; the user confirmed this outfit's update in the Save page's or
	// Manage Wardrobe's popup.
	// Its pieces whose plugin is not loaded stay in it; the HUD message says how many, and its
	// old name when it is renamed. Without `icon` it keeps its own.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> UpdateOutfit(std::uint32_t id, const std::string& name, const std::vector<RE::TESObjectARMO*>& armors,
		const std::optional<OutfitIcons::Icon>& icon = std::nullopt)
	{
		const auto* outfit = Library::Find(id);
		if (!outfit) {
			return "The outfit no longer exists.";
		}
		const auto  oldName = outfit->name;  // the update leaves `outfit` stale
		std::size_t unloaded = 0;
		Library::LoadedPieces(*outfit, &unloaded);
		if (auto error = Library::Update(id, name, armors, icon.value_or(outfit->icon))) {
			return error;
		}
		auto text = name == oldName ? std::format("Livery: updated {} ({} items)", name, armors.size()) :
		                              std::format("Livery: updated {}, renamed from {} ({} items)", name, oldName, armors.size());
		if (unloaded) {
			text += std::format("; {} item(s) from plugins that are not loaded stay in it", unloaded);
		}
		Notify(text + ".");
		return std::nullopt;
	}

	// Contract: game thread; the user chose Yes in the Apply popup for the library outfit
	// Livery has on the actor. Only its pieces change; its name stays.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> UpdateWornPieces(std::uint32_t id, const std::vector<RE::TESObjectARMO*>& armors)
	{
		const auto* outfit = Library::Find(id);
		if (!outfit) {
			return "The outfit no longer exists.";
		}
		// Copied: the update leaves `outfit` stale.
		const std::string name = outfit->name;
		return UpdateOutfit(id, name, armors);
	}

	// --- Preview --------------------------------------------------------------

	// The capture window set the player's kDisableSaving flag, so only it clears the flag.
	bool g_windowBlocksSaving = false;

	// Contract: game thread; from the window's opening to its close, Live Preview on or
	// off. The engine refuses a save silently while the window's menu is open, which
	// autosave mods may retry every frame, and one made during the preview would keep
	// its override and the free camera; the flag tells them not to ask (as Tailor's own
	// preview does). A flag another mod set is left alone.
	void BlockSaving()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		auto& flags = player->GetGameStatsData().byCharGenFlag;
		if (!flags.any(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving)) {
			flags.set(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
			g_windowBlocksSaving = true;
		}
	}

	// Contract: game thread.
	void AllowSaving()
	{
		if (!std::exchange(g_windowBlocksSaving, false)) {
			return;
		}
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			player->GetGameStatsData().byCharGenFlag.reset(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
		}
	}

	// Wires the live preview into the window about to open, and starts it when
	// the settings say so. While it is on, ticks change the pieces OutfitPreview
	// dresses the actor in, and input over the game moves PreviewCamera (when it
	// could take the camera); the window's Live Preview tick starts and ends both.
	// When the preview cannot start, the window greys the tick and says why.
	// Contract: game thread; `armors` matches request.pieces index for index.
	void AttachPreview(CaptureWindow::Request& request, RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& armors)
	{
		request.onPreviewOn = [handle = actor.GetHandle(), armors](const std::vector<std::size_t>& pieces) -> std::string {
			const auto target = handle.get();
			if (!target) {
				return "Preview off: the character is gone.";
			}
			auto reason = OutfitPreview::Begin(*target, PickPieces(armors, pieces));
			if (reason.empty()) {
				PreviewCamera::Begin(*target);  // without the camera the preview goes on in the game's own view
			}
			return reason;
		};
		request.onPreviewOff = [] {
			PreviewCamera::End();
			OutfitPreview::End();
		};
		// These three do nothing while Live Preview is off: no preview, no camera.
		request.onPieceToggled = [armors](std::size_t index, bool on) { OutfitPreview::SetPiece(armors[index], on); };
		request.onPreviewPieces = [armors](const std::vector<std::size_t>& pieces) { OutfitPreview::Show(PickPieces(armors, pieces)); };
		request.onViewInput = PreviewCamera::Move;

		if (Settings::LivePreview()) {
			std::vector<std::size_t> ticked;
			for (std::size_t i = 0; i < request.pieces.size(); ++i) {
				if (request.pieces[i].selected) {
					ticked.push_back(i);
				}
			}
			request.previewNote = request.onPreviewOn(ticked);
			request.previewOn = request.previewNote.empty();
		}
	}

	// --- Capture --------------------------------------------------------------

	// Followers via the teammate flag; spouses via Skyrim.esm's PlayerMarriedFaction.
	// Spouses added by marriage mods that use another faction are not recognised.
	bool IsFollowerOrSpouse(RE::Actor& actor)
	{
		if (actor.IsPlayerTeammate()) {
			return true;
		}
		// Skyrim.esm always loads at index 00, so its FormIDs are fixed.
		constexpr RE::FormID kPlayerMarriedFaction = 0x000C6472;
		const auto*          married = RE::TESForm::LookupByID<RE::TESFaction>(kPlayerMarriedFaction);
		return married && actor.IsInFaction(married);
	}

	std::string DisplayName(const RE::TESObjectARMO& armor)
	{
		const auto* name = armor.GetName();
		return name && *name ? name : std::format("{:08X}", armor.GetFormID());
	}

	struct InventoryArmor
	{
		RE::TESObjectARMO* armor;
		bool               worn;
	};

	// The plugin armor an inventory armor is listed as. The library names a piece by its
	// plugin, which runtime-created armor (FormID 0xFF......) does not have, so player-enchanted
	// gear is replaced by the plugin armor it was made from (templateArmor, TNAM); nullptr for
	// runtime-created armor without one.
	RE::TESObjectARMO* PluginArmor(RE::TESObjectARMO& armor)
	{
		if (!armor.IsDynamicForm()) {
			return &armor;
		}
		auto* base = armor.templateArmor;
		return base && !base->IsDynamicForm() ? base : nullptr;
	}

	// How many of the entry's copies are the actor's own outfit items: those whose
	// ExtraOutfitItem names `outfit`, their base's defaultOutfit. The trade menu leaves exactly
	// these out (docs/design.md §3); 0 for an actor without a default outfit.
	std::int32_t OwnOutfitCopies(const RE::InventoryEntryData& entry, RE::FormID outfit)
	{
		std::int32_t copies = 0;
		if (outfit == 0 || !entry.extraLists) {
			return copies;
		}
		for (auto* extra : *entry.extraLists) {
			const auto* mark = extra ? extra->GetByType<RE::ExtraOutfitItem>() : nullptr;
			if (mark && mark->id == outfit) {
				copies += extra->GetCount();
			}
		}
		return copies;
	}

	// Inventory armor as plugin-defined forms (PluginArmor), worn pieces first, each group in
	// name order; runtime-created armor without a plugin armor is left out. Only
	// worn pieces count in substituted and skipped. A form both worn and carried
	// is listed once, as worn.
	// Non-playable armor is left out uncounted: body-mod parts such as the BHUNP /
	// 3BA SMP objects (slot 48/50/51/60 per their MCM) are not clothing, and Dressing
	// leaves non-playable armor on.
	// The actor's own outfit copies (OwnOutfitCopies) are left out while unworn, as the trade
	// menu leaves them out, so the list shows what the trade menu shows; a worn one is listed
	// as worn, and a plain copy of the same armor beside them is listed.
	std::vector<InventoryArmor> CollectArmor(RE::Actor& actor, std::size_t& skipped, std::size_t& substituted)
	{
		const auto  inventory = actor.GetInventory([](RE::TESBoundObject& item) { return item.IsArmor(); });
		const auto* base = actor.GetActorBase();
		const auto  outfit = base && base->defaultOutfit ? base->defaultOutfit->GetFormID() : 0;

		std::vector<InventoryArmor> armors;
		armors.reserve(inventory.size());
		skipped = 0;
		substituted = 0;

		for (const auto& [object, itemData] : inventory) {
			const auto& [count, entry] = itemData;
			if (count <= 0) {
				continue;
			}
			auto* armor = object->As<RE::TESObjectARMO>();
			if (!armor || armor->formFlags & RE::TESObjectARMO::RecordFlags::kNonPlayable) {
				continue;
			}
			const bool worn = entry && entry->IsWorn();
			if (!worn && entry && count <= OwnOutfitCopies(*entry, outfit)) {
				continue;
			}
			auto* plugin = PluginArmor(*armor);
			if (!plugin) {
				if (worn) {
					++skipped;
					logger::info("Skipped runtime-created armor {:08X}", armor->GetFormID());
				}
				continue;
			}
			if (plugin != armor && worn) {
				++substituted;
				logger::info("Using base armor {:08X} for runtime-created armor {:08X}", plugin->GetFormID(), armor->GetFormID());
			}
			armor = plugin;
			if (const auto found = std::ranges::find(armors, armor, &InventoryArmor::armor); found != armors.end()) {
				found->worn = found->worn || worn;
			} else {
				armors.push_back({ armor, worn });
			}
		}

		std::ranges::sort(armors, [](const InventoryArmor& a, const InventoryArmor& b) {
			if (a.worn != b.worn) {
				return a.worn;
			}
			return _stricmp(DisplayName(*a.armor).c_str(), DisplayName(*b.armor).c_str()) < 0;
		});
		return armors;
	}

	// The player's armor an NPC's window lists under Player's Inventory, as plugin armor
	// (PluginArmor), each form once, per the settings: none with ShowPlayerInventory off, never a
	// quest item, a favorite only with ProtectFavorites off, and a form the player wears only with
	// ShowPlayerWornPieces on or when they carry another copy of it.
	std::vector<RE::TESObjectARMO*> OfferedPlayerArmor(RE::PlayerCharacter& player)
	{
		std::vector<RE::TESObjectARMO*> offered;
		if (!Settings::ShowPlayerInventory()) {
			return offered;
		}
		const bool showWorn = Settings::ShowPlayerWornPieces();
		const bool protectFavorites = Settings::ProtectFavorites();
		const auto inventory = player.GetInventory([](RE::TESBoundObject& item) { return item.IsArmor(); });
		for (const auto& [object, itemData] : inventory) {
			const auto& [count, entry] = itemData;
			auto* armor = object->As<RE::TESObjectARMO>();
			if (count <= 0 || !armor || armor->formFlags & RE::TESObjectARMO::RecordFlags::kNonPlayable) {
				continue;
			}
			if (entry && (entry->IsQuestObject() || (protectFavorites && entry->IsFavorited()))) {
				continue;
			}
			// One copy at most is worn, so a count of two or more leaves one carried.
			if (entry && entry->IsWorn() && !showWorn && count < 2) {
				continue;
			}
			auto* plugin = PluginArmor(*armor);
			if (plugin && std::ranges::find(offered, plugin) == offered.end()) {
				offered.push_back(plugin);
			}
		}
		return offered;
	}

	std::vector<RE::TESObjectARMO*> WornArmors(const std::vector<InventoryArmor>& armors)
	{
		std::vector<RE::TESObjectARMO*> worn;
		for (const auto& [armor, isWorn] : armors) {
			if (isWorn) {
				worn.push_back(armor);
			}
		}
		return worn;
	}

	// Wigs occupy the Hair slot, sometimes with Long Hair, and nothing else;
	// vanilla helmets and hoods mostly add Circlet, Head or Body. The vanilla
	// ones on Hair alone are listed by form. Wigs are offered unticked so a
	// captured outfit does not replace another character's hair by default.
	bool LooksLikeWig(const RE::TESObjectARMO& armor)
	{
		struct PluginForm
		{
			std::string_view plugin;
			RE::FormID       localID;
		};
		constexpr std::array<PluginForm, 7> kHairOnlyHeadwear{ {
			{ "Skyrim.esm", 0x1019CA },     // ArmorDraugrHelmetUnplayable
			{ "Skyrim.esm", 0x04C3CB },     // ArmorFAlmerHelmet
			{ "Skyrim.esm", 0x0DA750 },     // ArmorBoneCrown
			{ "Skyrim.esm", 0x0D3EAA },     // ArmorPenitusHelmet
			{ "Skyrim.esm", 0x0CF8B2 },     // ClothesExecutionerHood
			{ "Skyrim.esm", 0x036A45 },     // ClothesRobesGreybeardHood
			{ "Dawnguard.esm", 0x017E9B },  // DLC1VCClothesWarlockHoodUnplayable
		} };

		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		auto others = armor.GetSlotMask();
		others.reset(Slot::kHair, Slot::kLongHair);
		if (!armor.HasPartOf(Slot::kHair) || others) {
			return false;
		}
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return true;
		}
		// LookupFormID returns 0 for a plugin that is not loaded, which matches no armor.
		return std::ranges::none_of(kHairOnlyHeadwear, [&](const PluginForm& form) {
			return dataHandler->LookupFormID(form.localID, form.plugin) == armor.GetFormID();
		});
	}

	// The body slots of `mask` as the Creation Kit numbers them, lowest first: "32, 34"; "" for none.
	std::string SlotText(std::uint32_t mask)
	{
		constexpr int kFirstSlot = 30;  // the number of bit 0's slot (kHead)

		std::string text;
		for (int bit = 0; bit < 32; ++bit) {
			if ((mask >> bit) & 1u) {
				text += std::format("{}{}", text.empty() ? "" : ", ", kFirstSlot + bit);
			}
		}
		return text;
	}

	// A row of the window's piece list, with SkyUI's Type (ArmorInfo), its body slots and its
	// icon. An I4 rule's Type text replaces SkyUI's, as SkyUI with I4 shows it; the sort order
	// stays the slot's.
	CaptureWindow::Piece MakePiece(const RE::TESObjectARMO& armor, CaptureWindow::PieceGroup group, bool selected)
	{
		const auto type = ArmorInfo::TypeOf(armor);
		const auto slots = static_cast<std::uint32_t>(armor.GetSlotMask().underlying());
		auto       icon = PieceIcons::Resolve(armor);
		return { .name = DisplayName(armor),
			.selected = selected,
			.slots = slots,
			.group = group,
			.type = icon.typeName.empty() ? std::string(type.name) : std::move(icon.typeName),
			.typeOrder = type.order,
			.slotText = SlotText(slots),
			.slotOrder = std::countr_zero(slots),
			.icon = std::move(icon.key),
			.iconColor = icon.color };
	}

	// The group of a piece the actor carries: worn and put on by Livery from the library
	// outfit `livery` (or not a library one), worn otherwise, or only carried.
	CaptureWindow::PieceGroup OwnPieceGroup(const InventoryArmor& piece, const std::optional<Assignments::Choices>& choices, const Library::Outfit* livery)
	{
		using Group = CaptureWindow::PieceGroup;
		if (!piece.worn) {
			return Group::kCarried;
		}
		const bool byLivery = choices && std::ranges::find(choices->wornArmor, piece.armor->GetFormID()) != choices->wornArmor.end();
		return !byLivery ? Group::kUnmanaged : (livery ? Group::kLiveryOutfit : Group::kNotInLibrary);
	}

	// Worn pieces start ticked, wigs (LooksLikeWig) not.
	bool StartsTicked(const InventoryArmor& piece)
	{
		return piece.worn && !LooksLikeWig(*piece.armor);
	}

	// Index of the armor in `armors`; armor not there yet is appended to both
	// lists as a piece nobody carries, unticked.
	std::size_t PieceIndex(RE::TESObjectARMO* armor, std::vector<RE::TESObjectARMO*>& armors, std::vector<CaptureWindow::Piece>& pieces)
	{
		if (const auto found = std::ranges::find(armors, armor); found != armors.end()) {
			return static_cast<std::size_t>(found - armors.begin());
		}
		armors.push_back(armor);
		pieces.push_back(MakePiece(*armor, CaptureWindow::PieceGroup::kNotCarried, false));
		return armors.size() - 1;
	}

	// An actor as the window's Remove popups name them: their name, or their FormID when not
	// in memory. Contract: game thread.
	std::string ActorLabel(RE::FormID id)
	{
		auto*       actor = RE::TESForm::LookupByID<RE::Actor>(id);
		const auto* name = actor ? actor->GetDisplayFullName() : nullptr;
		return name && *name ? name : std::format("{:08X}", id);
	}

	// Who has library outfit `id` among Livery's assignments, one line each for Remove's
	// popup: "Lydia: Default, Town", the FormID of an actor not in memory.
	// Contract: game thread.
	std::vector<std::string> UserLines(std::uint32_t id)
	{
		std::vector<std::string> lines;
		for (const auto& use : Assignments::UsesOf(id)) {
			std::string line = ActorLabel(use.actor);
			for (std::size_t i = 0; i < use.rows.size(); ++i) {
				line += std::format("{}{}", i == 0 ? ": " : ", ", SituationStore::Name(use.rows[i]));
			}
			lines.push_back(std::move(line));
		}
		return lines;
	}

	// Who has outfits on the situation's list among Livery's assignments, one line each for
	// the Remove situation? popup: "Lydia: 2 outfit(s)". Contract: game thread.
	std::vector<std::string> HolderLines(Situations::Situation situation)
	{
		std::vector<std::string> lines;
		for (const auto& holder : Assignments::HoldersOf(situation)) {
			lines.push_back(std::format("{}: {} outfit(s)", ActorLabel(holder.actor), holder.outfits));
		}
		return lines;
	}

	// The window's rows (Request::situations): the Default, then the situations not removed
	// (SituationStore::Offered), each with its name, conditions and who has outfits on it.
	// Contract: game thread.
	std::vector<CaptureWindow::SituationInfo> SituationInfos()
	{
		std::vector<CaptureWindow::SituationInfo> infos;
		const auto                                add = [&infos](Situations::Situation situation, std::vector<std::string> holders) {
			infos.push_back({ .situation = situation,
				.name = SituationStore::Name(situation),
				.conditions = Situations::ConditionsText(situation),
				.holders = std::move(holders) });
		};
		add(Situations::Situation::kDefault, {});
		for (const auto situation : SituationStore::Offered()) {
			add(situation, HolderLines(situation));
		}
		return infos;
	}

	// The library's outfits in case-insensitive name order, each with who has it and its
	// loaded pieces as indices from `indexOf`, which leaves out armor it returns nullopt for.
	// Contract: game thread (resolves the library's pieces to armor forms).
	std::vector<CaptureWindow::ExistingOutfit> ListOutfits(const std::function<std::optional<std::size_t>(RE::TESObjectARMO*)>& indexOf)
	{
		std::vector<const Library::Outfit*> library;
		for (const auto& outfit : Library::Outfits()) {
			library.push_back(&outfit);
		}
		std::ranges::sort(library, [](const Library::Outfit* a, const Library::Outfit* b) { return _stricmp(a->name.c_str(), b->name.c_str()) < 0; });

		std::vector<CaptureWindow::ExistingOutfit> outfits;
		outfits.reserve(library.size());
		for (const auto* stored : library) {
			CaptureWindow::ExistingOutfit outfit{
				.name = stored->name,
				.icon = stored->icon,
				.id = stored->id,
				.users = UserLines(stored->id)
			};
			// LoadedPieces lists each form once, so the indices do not repeat.
			for (auto* armor : Library::LoadedPieces(*stored, &outfit.unloadedPieces)) {
				if (const auto index = indexOf(armor)) {
					outfit.pieces.push_back(*index);
				}
			}
			std::ranges::sort(outfit.pieces);
			outfits.push_back(std::move(outfit));
		}
		return outfits;
	}

	// The outfits as the window opens with them: `armors` gains the pieces the actor does
	// not carry (`pieces` matches it index for index).
	std::vector<CaptureWindow::ExistingOutfit> ExistingOutfits(std::vector<RE::TESObjectARMO*>& armors, std::vector<CaptureWindow::Piece>& pieces)
	{
		return ListOutfits([&](RE::TESObjectARMO* armor) -> std::optional<std::size_t> { return PieceIndex(armor, armors, pieces); });
	}

	// The outfits again for the open window, whose pieces are fixed: every loaded piece of
	// every outfit was listed at Open, and its edits pick only listed pieces. A piece not
	// found is left out, with an error in the log, so no index runs past the window's list.
	std::vector<CaptureWindow::ExistingOutfit> RebuiltOutfits(const std::vector<RE::TESObjectARMO*>& armors)
	{
		return ListOutfits([&](RE::TESObjectARMO* armor) -> std::optional<std::size_t> {
			if (const auto found = std::ranges::find(armors, armor); found != armors.end()) {
				return static_cast<std::size_t>(found - armors.begin());
			}
			logger::error("Outfit piece {:08X} is not in the capture window's list; left out of the rebuilt list", armor->GetFormID());
			return std::nullopt;
		});
	}

	// How the armor they wear differs from what Livery put on them, by `inventory`
	// (CollectArmor): `notWorn`, Livery's pieces they wear no copy of (taken back in the trade
	// menu, or taken off by another mod or by the player in their own inventory); `extra`, worn
	// armor Livery did not put on (the Inventory page's Outfit (Unmanaged)). Both 0 when Livery
	// has nothing on them.
	struct WornGaps
	{
		std::size_t notWorn = 0;
		std::size_t extra = 0;
	};

	WornGaps GapsOf(const std::optional<Assignments::Choices>& choices, const std::vector<InventoryArmor>& inventory)
	{
		if (!choices || !choices->dressed) {
			return {};
		}
		const auto& livery = choices->wornArmor;
		const auto  notWorn = std::ranges::count_if(livery, [&inventory](RE::FormID armor) {
			const auto found = std::ranges::find_if(inventory, [armor](const InventoryArmor& piece) { return piece.armor->GetFormID() == armor; });
			return found == inventory.end() || !found->worn;
		});
		const auto  extra = std::ranges::count_if(inventory, [&livery](const InventoryArmor& piece) {
			return piece.worn && std::ranges::find(livery, piece.armor->GetFormID()) == livery.end();
		});
		return { static_cast<std::size_t>(notWorn), static_cast<std::size_t>(extra) };
	}

	// Contract: game thread. The captured actor's GapsOf, read from their inventory with no
	// preview on: as the window opens and after each dressing with it open (DressCaptured).
	// The window freezes the game, so what they wear holds until the next.
	WornGaps g_capturedGaps;

	// `, 1 item not worn`, `, 2 extra items worn`, both, or "".
	std::string GapsText(const WornGaps& gaps)
	{
		const auto count = [](std::size_t n, const char* what, const char* after) {
			return n == 0 ? std::string{} : std::format(", {} {}{} {}", n, what, n == 1 ? "" : "s", after);
		};
		return count(gaps.notWorn, "item", "not worn") + count(gaps.extra, "extra item", "worn");
	}

	// The line under the actor's name: what Livery has on them now and what for, `Wearing "X"
	// for Town.` or `Wearing "X" as Default.` Items that are not a saved outfit, or an outfit
	// gone from the library (another save's), which the window shows no row of, go unnamed:
	// `Dressed by Livery.` With Livery's pieces off them the outfit is only theirs, and the
	// differences follow: `Assigned "X" as Default, 1 piece not worn, 2 extra pieces worn.`
	std::string DescribeAssignment(const Assignments::Choices& choices, const WornGaps& gaps)
	{
		if (!choices.dressed) {
			return "Not dressed by Livery right now.";
		}
		const auto* stored = choices.wornOutfit ? Library::Find(choices.wornOutfit) : nullptr;
		if (!stored) {
			return std::format("Dressed by Livery{}.", GapsText(gaps));
		}
		const auto worn = choices.worn == Situations::Situation::kDefault ? " as " + SituationStore::Name(choices.worn) : SituationStore::Suffix(choices.worn);
		return std::format("{} \"{}\"{}{}.", gaps.notWorn == 0 ? "Wearing" : "Assigned", stored->name, worn, GapsText(gaps));
	}

	// The library outfit Livery has on them now, for the first heading of the piece list;
	// nullptr when Livery has nothing on them, the pieces are not a saved outfit (the
	// Default's pieces) or the outfit has left the library.
	// Contract: the pointer goes stale at the next Library::Create or Update.
	const Library::Outfit* WornLibraryOutfit(const std::optional<Assignments::Choices>& choices)
	{
		if (!choices || !choices->dressed) {
			return nullptr;
		}
		return choices->wornOutfit ? Library::Find(choices->wornOutfit) : nullptr;
	}

	// The Inventory page's Apply after the move: `picked` as the actor's Default, `outfit` its
	// library outfit (0 for none), without the pieces that did not move (MovePlayerPieces).
	// Contract: game thread; no preview is on (DressCaptured, or the window closed).
	void AssignMoved(RE::Actor& target, std::vector<RE::TESObjectARMO*> picked, const std::vector<RE::TESObjectARMO*>& notMoved, std::uint32_t outfit)
	{
		std::erase_if(picked, [&notMoved](const RE::TESObjectARMO* armor) { return std::ranges::find(notMoved, armor) != notMoved.end(); });
		Assignments::Assign(target, picked, outfit);
	}

	// Moves `moving`, the pieces ticked under Player's Inventory, to the NPC, when
	// MoveFromPlayer is on, and says on the HUD how many went. Returns those that did not
	// move, which the dressing then leaves out: never a copy in their place.
	// Contract: game thread; no preview is on (DressCaptured, or the window closed).
	std::vector<RE::TESObjectARMO*> MovePlayerPieces(RE::Actor& npc, const std::vector<RE::TESObjectARMO*>& moving)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || npc.IsPlayerRef() || !Settings::MoveFromPlayer() || moving.empty()) {
			return {};
		}
		auto        notMoved = Dressing::MoveFromPlayer(*player, npc, moving, Settings::ShowPlayerWornPieces());
		const auto* name = npc.GetDisplayFullName();
		auto        text = std::format("Livery: moved {} of {} item(s) from you to {}", moving.size() - notMoved.size(), moving.size(), name && *name ? name : "them");
		for (std::size_t i = 0; i < notMoved.size(); ++i) {
			text += (i == 0 ? "; not moved: " : ", ") + DisplayName(*notMoved[i]);
		}
		Notify(text + ".");
		return notMoved;
	}

	// A row of the window (Request::assigned) as Assignments names it: Default, then the
	// situations not removed (SituationStore::Offered).
	// Contract: SituationStore has not changed since the window got its rows (Open, SetOutfits).
	Situations::Situation RowSituation(std::size_t row)
	{
		return row == 0 ? Situations::Situation::kDefault : SituationStore::Offered()[row - 1];
	}

	bool InLibrary(std::uint32_t id)
	{
		return Library::Find(id) != nullptr;
	}

	// The actor's rows as they have them now, in RowSituation's order, without the outfits
	// gone from the library (OutfitRows::WithoutGone), which RowsWith keeps.
	std::vector<CaptureWindow::RowAssignment> AssignedRows(const std::optional<Assignments::Choices>& choices)
	{
		const auto                                offered = SituationStore::Offered();
		std::vector<CaptureWindow::RowAssignment> rows(offered.size() + 1);
		if (choices) {
			const auto shown = [&choices](Situations::Situation situation) { return OutfitRows::WithoutGone(choices->rows[OutfitRows::RowOf(situation)], InLibrary); };
			rows[0] = { .row = shown(Situations::Situation::kDefault), .pieces = choices->defaultPieces };
			for (std::size_t i = 0; i < offered.size(); ++i) {
				rows[i + 1] = { .row = shown(offered[i]) };
			}
		}
		return rows;
	}

	void RedressFor(RE::Actor& actor, std::uint32_t id);

	// The Save page: the actor's rows with what they have in each now, which its Create and
	// Apply popup names, and the save. The library is written first; only once it took the
	// outfit does the window close, which ends the preview. Then Create and Apply moves the
	// Player's Inventory pieces and gives the outfit the one row the user applied it to, in
	// one call that redresses once; Update dresses the actor again when they wear the outfit.
	// Contract: game thread; `armors` matches request.pieces index for index. A refused or
	// failed write assigns nothing (CLAUDE.md §2.1).
	void AttachSave(CaptureWindow::Request& request, RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& armors,
		const std::optional<Assignments::Choices>& choices)
	{
		request.situations = SituationInfos();
		request.assigned = AssignedRows(choices);
		request.onSave = [handle = actor.GetHandle(), armors](const CaptureWindow::SaveRequest& save) {
			const auto picked = PickPieces(armors, save.pieces);
			const auto error = save.outfit ? UpdateOutfit(save.outfit, save.name, picked, save.icon) :
			                                 SaveOutfit(save.name, picked, save.icon.value_or(OutfitIcons::Icon{}));
			if (error) {
				CaptureWindow::SetError(*error);
				return;
			}
			const auto* saved = save.outfit ? Library::Find(save.outfit) : Library::FindByName(save.name);
			const auto  outfit = saved ? saved->id : 0;
			CaptureWindow::Close();
			const auto target = handle.get();
			if (outfit == 0 || !target) {
				return;
			}
			if (!save.applyRow) {
				if (save.outfit) {
					RedressFor(*target, outfit);
				}
				return;
			}
			// Create and Apply: the pieces move before the row dresses anyone, so the dressing
			// finds them carried; those that did not move stay out of it, never a copy.
			Assignments::Changes changes{ .outfit = outfit, .assign = { RowSituation(*save.applyRow) } };
			changes.leaveOut = MovePlayerPieces(*target, PickPieces(armors, save.fromPlayer));
			Assignments::Change(*target, changes);
		};
	}

	// What a Manage Wardrobe or Manage Situations change redresses the captured actor for
	// (RedressCaptured; docs/outfit-management-ui.md §6).
	struct PendingRedress
	{
		std::uint32_t updated = 0;  // the outfit Update changed; 0 when a situation outfit they wear left the library
		// Their rows changed already (Assignments::SetRows): dressed for anew
		// (Assignments::Refresh), the row they wear wearing `pick` when it gained one.
		bool          refresh = false;
		std::uint32_t pick = 0;
	};

	// Actors other than the captured one taken out of a removed situation outfit, or of a
	// removed situation, at the window's close, after its preview ended.
	// Contract: game thread; emptied by the close that runs them, and at kPreLoadGame
	// before that load closes the window.
	std::vector<RE::FormID> g_redressAtClose;

	void QueueRedress(RE::FormID actor)
	{
		if (std::ranges::find(g_redressAtClose, actor) == g_redressAtClose.end()) {
			g_redressAtClose.push_back(actor);
		}
	}

	// Whether Livery has library outfit `id` on the actor now, for any row.
	bool WearsOutfit(const std::optional<Assignments::Choices>& choices, std::uint32_t id)
	{
		return choices && choices->dressed && id != 0 && choices->wornOutfit == id;
	}

	// Dresses them in library outfit `id` as it is now when they wear it, read from the
	// library again (an NPC gets a copy of a piece they lack; nothing moves from the player).
	// Not worn now: nothing, as every row reads its outfit each time it goes on.
	// Contract: game thread; no preview is on.
	void RedressFor(RE::Actor& actor, std::uint32_t id)
	{
		if (WearsOutfit(Assignments::ChoicesOf(actor.GetFormID()), id)) {
			Assignments::Refresh(actor, {});
		}
	}

	// An updated outfit as RedressFor puts it on; a situation outfit gone from the library
	// gives way to what applies now: another situation's outfit, or the default; changed rows
	// are dressed for (Assignments::Refresh).
	// Contract: game thread; no preview is on.
	void RunRedress(RE::Actor& actor, const PendingRedress& redress)
	{
		if (redress.refresh) {
			Assignments::Refresh(actor, {}, redress.pick);
		} else if (redress.updated != 0) {
			RedressFor(actor, redress.updated);
		} else {
			Assignments::WearFor(actor, Situations::Evaluate(actor));
		}
	}

	// The captured actor's pieces as the open window lists them now (CaptureWindow::ActorPieces),
	// from `inventory`, their armor now: each piece of `armors` in the group Open would give it
	// (`offered`: the player's armor the window lists), the worn ones ticked. Armor they carry
	// that `armors` does not have stays unlisted, since the window's indices are fixed.
	// Contract: game thread; `armors` matches the window's Request::pieces index for index.
	CaptureWindow::ActorPieces CurrentActorPieces(RE::Actor& actor, const std::vector<InventoryArmor>& inventory, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<RE::TESObjectARMO*>& offered)
	{
		using Group = CaptureWindow::PieceGroup;
		const auto                 choices = Assignments::ChoicesOf(actor.GetFormID());
		const auto*                livery = WornLibraryOutfit(choices);
		CaptureWindow::ActorPieces pieces{ .groups = std::vector<Group>(armors.size(), Group::kNotCarried) };
		if (livery) {
			pieces.outfitName = livery->name;
			pieces.wornOutfit = livery->id;
			pieces.wornFor = choices->worn;
			if (choices->worn != Situations::Situation::kDefault) {
				pieces.wornSituation = SituationStore::Name(choices->worn);
			}
		}
		for (std::size_t i = 0; i < armors.size(); ++i) {
			const auto own = std::ranges::find(inventory, armors[i], &InventoryArmor::armor);
			if (own == inventory.end()) {
				pieces.groups[i] = std::ranges::find(offered, armors[i]) != offered.end() ? Group::kPlayer : Group::kNotCarried;
				continue;
			}
			pieces.groups[i] = OwnPieceGroup(*own, choices, livery);
			if (StartsTicked(*own)) {
				pieces.ticked.push_back(i);
			}
		}
		return pieces;
	}

	// Runs `dress` on the captured actor with the window open. Assignments run only with no
	// preview on, as the preview puts back what it found at Begin: with it on, it ends, `dress`
	// runs and it begins again on `shown`, or with none on what they wear now, the camera and
	// the movement hold staying; a refusal turns Live Preview off with the reason. With it off
	// `dress` runs in the game the window froze, and an NPC's model is rebuilt after it. Then
	// returns their pieces for the window (SetOutfits).
	// Contract: game thread; `actor` is the captured actor; `armors` matches the window's
	// Request::pieces index for index, `offered` are the player's armor among them.
	CaptureWindow::ActorPieces DressCaptured(RE::Actor& actor, const std::function<void()>& dress, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<RE::TESObjectARMO*>& offered, const std::vector<RE::TESObjectARMO*>* shown)
	{
		const bool previewing = OutfitPreview::IsActive();
		if (previewing) {
			OutfitPreview::End();
		}
		dress();
		// Contract: with no preview on, the window froze the game, which skips the NPC's actor
		// update that builds their new pieces' 3D, while a copy Livery took back lost its 3D at
		// once (their torso shows empty): rebuild the model now. Dress rebuilds the player's.
		if (!previewing && !actor.IsPlayerRef()) {
			Dressing::RefreshModel(actor);
		}
		std::size_t skipped = 0;
		std::size_t substituted = 0;
		const auto  inventory = CollectArmor(actor, skipped, substituted);
		g_capturedGaps = GapsOf(Assignments::ChoicesOf(actor.GetFormID()), inventory);  // before the preview begins again
		if (previewing) {
			if (const auto reason = OutfitPreview::Begin(actor, shown ? *shown : WornArmors(inventory)); !reason.empty()) {
				PreviewCamera::End();
				CaptureWindow::RefusePreview(reason);
			}
		}
		return CurrentActorPieces(actor, inventory, armors, offered);
	}

	// Redresses the captured actor at once, Live Preview on or off (DressCaptured), returning
	// their pieces for the window (SetOutfits).
	// Contract: as DressCaptured's.
	CaptureWindow::ActorPieces RedressCaptured(RE::Actor& actor, const PendingRedress& redress, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<RE::TESObjectARMO*>& offered, const std::vector<RE::TESObjectARMO*>* shown)
	{
		return DressCaptured(actor, [&actor, &redress] { RunRedress(actor, redress); }, armors, offered, shown);
	}

	// The queued actors taken out of what they wore only while loaded and alive, as the poll
	// does; the others are dressed when they load (Assignments::ActorsWithSituations).
	// Contract: game thread, in the window's onClose after OutfitPreview::End.
	void RedressAtClose()
	{
		for (const auto id : std::exchange(g_redressAtClose, {})) {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!actor || !actor->Is3DLoaded() || actor->IsDead()) {
				continue;
			}
			Assignments::WearFor(*actor, Situations::Evaluate(*actor));
		}
	}

	// Manage Wardrobe's Apply: the outfit goes to window row `row` (RowSituation), the one the
	// window chose: the situation they wear another outfit for now, after the user agreed in
	// its popup, the one they wear it for, or the Default; then they are dressed for their
	// situations now with the window open (DressCaptured): the preview begins again on
	// `shown`, the outfit as Manage Wardrobe shows it. Returns their pieces for the window
	// (SetOutfits).
	// Contract: as DressCaptured's.
	CaptureWindow::ActorPieces ApplyOutfit(RE::Actor& actor, std::uint32_t outfit, std::size_t row, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<RE::TESObjectARMO*>& offered, const std::vector<RE::TESObjectARMO*>& shown)
	{
		const auto dress = [&actor, outfit, row] { Assignments::Change(actor, { .outfit = outfit, .assign = { RowSituation(row) } }); };
		return DressCaptured(actor, dress, armors, offered, &shown);
	}

	// What the open window shows of the library and of the actor's rows now (SetOutfits).
	// Contract: game thread; `armors` matches the window's Request::pieces index for index.
	CaptureWindow::OutfitList CurrentOutfitList(RE::FormID actorID, const std::vector<RE::TESObjectARMO*>& armors)
	{
		const auto                choices = Assignments::ChoicesOf(actorID);
		CaptureWindow::OutfitList list{ .existingOutfits = RebuiltOutfits(armors),
			.categories = CategoryOptions(),
			.assigned = AssignedRows(choices),
			.situations = SituationInfos(),
			.restoreChanges = SituationStore::RestoreChanges() };
		if (choices) {
			list.actorSummary = DescribeAssignment(*choices, g_capturedGaps);
			list.dressed = choices->dressed;
			list.piecesNotWorn = choices->dressed ? g_capturedGaps.notWorn : 0;
			list.extraWorn = choices->dressed ? g_capturedGaps.extra : 0;
		}
		return list;
	}

	// The window's Apply and Restore, and the line under the actor's name.
	// Apply dresses them in the ticked pieces as their default, the player's as an NPC's,
	// those the window lists under Player's Inventory moved to an NPC first (a piece
	// Livery put on them while the window was open is listed as theirs); with Yes in its
	// popup the worn library outfit takes the ticks before. The window stays open: they are
	// dressed with no preview on (DressCaptured), and the Inventory page follows what they
	// wear. Restore, shown while Livery has pieces on them, ends what Livery chose for them;
	// it closes the window first, so the preview has put back what they wore before.
	// Contract: game thread; `armors` matches request.pieces index for index, `offered` are the
	// player's armor among them. A refused or failed library write moves and assigns nothing
	// (CLAUDE.md §2.1).
	void AttachAssignment(CaptureWindow::Request& request, RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<RE::TESObjectARMO*>& offered, const std::optional<Assignments::Choices>& choices)
	{
		const auto handle = actor.GetHandle();
		const auto actorID = actor.GetFormID();
		request.movesFromPlayer = Settings::MoveFromPlayer();
		request.protectsFavorites = Settings::ProtectFavorites();
		request.onApply = [handle, actorID, armors, offered, name = request.actorName](const std::vector<std::size_t>& pieces,
							  const std::vector<std::size_t>& fromPlayer, std::uint32_t outfit, bool updateWorn) {
			const auto target = handle.get();
			if (!target) {
				CaptureWindow::SetError(std::format("{} is no longer here.", name));
				return;
			}
			// The row they wear as the window shows it, which follows a redress made while it is
			// open.
			const auto current = Assignments::ChoicesOf(actorID);
			const bool forSituation = current && current->worn != Situations::Situation::kDefault;
			const auto picked = PickPieces(armors, pieces);
			if (updateWorn) {
				if (const auto error = UpdateWornPieces(outfit, picked)) {
					CaptureWindow::SetError(*error);
					return;
				}
			}
			const auto dress = [&] {
				const auto notMoved = MovePlayerPieces(*target, PickPieces(armors, fromPlayer));
				// A situation outfit is read from the library, so the update is what goes on;
				// the default is not touched.
				if (updateWorn && forSituation) {
					Assignments::Refresh(*target, notMoved);
					return;
				}
				AssignMoved(*target, picked, notMoved, outfit);
			};
			auto dressed = DressCaptured(*target, dress, armors, offered, nullptr);
			auto list = CurrentOutfitList(actorID, armors);  // after the dressing
			list.actorPieces = std::move(dressed);
			CaptureWindow::SetOutfits(std::move(list));
		};
		request.onRestore = [handle] {
			CaptureWindow::Close();
			if (const auto target = handle.get()) {
				Assignments::Unassign(*target);
			}
		};
		if (choices) {
			request.actorSummary = DescribeAssignment(*choices, g_capturedGaps);
			request.dressed = choices->dressed;
			request.piecesNotWorn = choices->dressed ? g_capturedGaps.notWorn : 0;
			request.extraWorn = choices->dressed ? g_capturedGaps.extra : 0;
		}
	}

	// Manage Wardrobe' Update, Remove and Apply. Update and Remove write the library first; a
	// refused or failed write changes nothing, assigns nothing and redresses no one (CLAUDE.md
	// §2.1). Then the actors are redressed (RedressCaptured, QueueRedress, ApplyOutfit), and
	// the window, still open, shows the library as written. Apply writes nothing to the library.
	// Contract: game thread; `armors` matches request.pieces index for index.
	void AttachManage(CaptureWindow::Request& request, RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& armors, const std::vector<RE::TESObjectARMO*>& offered)
	{
		const auto handle = actor.GetHandle();
		const auto actorID = actor.GetFormID();
		// When they wear the outfit, it is dressed again as RedressFor does, at once, Live
		// Preview on or off (RedressCaptured keeps the preview in step, on the outfit as edited,
		// and the Inventory page follows what they wear). Update and Apply instead dresses them
		// as Apply does (ApplyOutfit).
		request.onManageUpdate = [handle, actorID, armors, offered](const CaptureWindow::SaveRequest& edit, bool apply) {
			const auto picked = PickPieces(armors, edit.pieces);
			if (const auto error = UpdateOutfit(edit.outfit, edit.name, picked, edit.icon)) {
				CaptureWindow::SetError(*error);
				return;
			}
			const auto                                target = handle.get();
			std::optional<CaptureWindow::ActorPieces> redressed;
			if (target && apply && edit.applyRow) {
				redressed = ApplyOutfit(*target, edit.outfit, *edit.applyRow, armors, offered, picked);
			} else if (target && !apply && WearsOutfit(Assignments::ChoicesOf(actorID), edit.outfit)) {
				redressed = RedressCaptured(*target, { .updated = edit.outfit }, armors, offered, &picked);
			}
			auto list = CurrentOutfitList(actorID, armors);
			list.actorPieces = std::move(redressed);
			CaptureWindow::SetOutfits(std::move(list));
		};
		// The outfit leaves every row, and a Default this leaves wearing nothing keeps its
		// pieces (Assignments::ForgetOutfit); one HUD message sums it up. The captured actor,
		// redressed at once, is shown as they are now, and the Inventory page follows.
		request.onManageRemove = [handle, actorID, armors, offered](std::uint32_t outfit) {
			const auto* stored = Library::Find(outfit);
			// Read before Remove, which leaves `stored` stale. The armor forms stay valid.
			const auto name = stored ? stored->name : std::string{};
			const auto pieces = stored ? Library::LoadedPieces(*stored) : std::vector<RE::TESObjectARMO*>{};
			const auto users = Assignments::UsesOf(outfit).size();
			if (const auto error = Library::Remove(outfit)) {
				CaptureWindow::SetError(*error);
				return;
			}
			const auto wearing = Assignments::ForgetOutfit(outfit, pieces);
			Notify(users > 0 ? std::format("Livery: removed \"{}\"; cleared from {} character(s).", name, users) : std::format("Livery: removed \"{}\".", name));
			const auto                                target = handle.get();
			std::optional<CaptureWindow::ActorPieces> redressed;
			for (const auto id : wearing) {
				if (id == actorID && target) {
					redressed = RedressCaptured(*target, {}, armors, offered, nullptr);
				} else {
					QueueRedress(id);
				}
			}
			auto list = CurrentOutfitList(actorID, armors);
			list.actorPieces = std::move(redressed);
			CaptureWindow::SetOutfits(std::move(list));
		};
		// Apply to #{Name}: the outfit as the library has it goes to the row the window chose
		// and on (ApplyOutfit), the window staying open; the Inventory page follows what they
		// wear.
		request.onManageApply = [handle, actorID, armors, offered, name = request.actorName](std::uint32_t outfit, std::size_t row) {
			const auto target = handle.get();
			if (!target) {
				CaptureWindow::SetError(std::format("{} is no longer here.", name));
				return;
			}
			const auto* stored = Library::Find(outfit);
			if (!stored) {
				CaptureWindow::SetError("The outfit no longer exists.");
				return;
			}
			const auto pieces = Library::LoadedPieces(*stored);
			if (pieces.empty()) {
				CaptureWindow::SetError("None of its items is loaded.");
				return;
			}
			auto dressed = ApplyOutfit(*target, outfit, row, armors, offered, pieces);
			auto list = CurrentOutfitList(actorID, armors);  // after the dressing
			list.actorPieces = std::move(dressed);
			CaptureWindow::SetOutfits(std::move(list));
		};
	}

	// The actor's rows with Manage Situations' rows put in, by situation, each keeping the
	// outfits gone from the library that the window did not show (OutfitRows::KeepGone).
	// Contract: game thread.
	OutfitRows::Rows RowsWith(RE::FormID actorID, const std::vector<CaptureWindow::RowEdit>& edits)
	{
		const auto       choices = Assignments::ChoicesOf(actorID);
		OutfitRows::Rows rows = choices ? choices->rows : OutfitRows::Rows{};
		for (const auto& [situation, row] : edits) {
			auto& stored = rows[OutfitRows::RowOf(situation)];
			stored = OutfitRows::KeepGone(row, stored, InLibrary);
		}
		return rows;
	}

	// Manage Situations' Save Changes, Remove (a situation) and Restore Presets. Each writes
	// situations.json first; a refused or failed write changes no row and redresses no one
	// (CLAUDE.md §2.1). Then the captured actor's rows change and the actors are redressed
	// (RedressCaptured, QueueRedress), and the window, still open, shows the situations and
	// the rows as they are now.
	// Contract: game thread; `armors` matches request.pieces index for index.
	void AttachSituations(CaptureWindow::Request& request, RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<RE::TESObjectARMO*>& offered)
	{
		const auto handle = actor.GetHandle();
		const auto actorID = actor.GetFormID();
		request.restoreChanges = SituationStore::RestoreChanges();
		// The rows are made at once, so the window shows them, and dressed for in one Refresh,
		// at once, Live Preview on or off (RedressCaptured, the preview begun again on the
		// outfit whose details show, else on what they wear). The row they wear, when random,
		// wears the outfit it gained first.
		request.onSituationsSave = [handle, actorID, armors, offered](const CaptureWindow::SituationSave& save) {
			if (!save.edits.empty()) {
				std::vector<SituationStore::Edit> edits;
				for (const auto& [situation, name] : save.edits) {
					edits.push_back({ .situation = situation, .name = name });
				}
				if (const auto error = SituationStore::Apply(edits)) {
					CaptureWindow::SetError(*error);
					return;
				}
				Notify("Livery: saved the situations' names.");
			}
			const auto                                target = handle.get();
			const auto                                shown = save.previewPieces ? PickPieces(armors, *save.previewPieces) : std::vector<RE::TESObjectARMO*>{};
			std::optional<CaptureWindow::ActorPieces> redressed;
			if (target && !save.rows.empty()) {
				if (const auto pick = Assignments::SetRows(*target, RowsWith(actorID, save.rows), save.dropDefaultPieces)) {
					redressed = RedressCaptured(*target, { .refresh = true, .pick = *pick }, armors, offered,
						save.previewPieces ? &shown : nullptr);
				}
			}
			auto list = CurrentOutfitList(actorID, armors);  // after the rows changed
			list.actorPieces = std::move(redressed);
			CaptureWindow::SetOutfits(std::move(list));
		};
		// Every row of the situation goes (Assignments::ForgetSituation), and those wearing it
		// are taken out of it as Remove takes them out of a removed outfit.
		request.onSituationRemove = [handle, actorID, armors, offered](Situations::Situation situation) {
			const auto name = SituationStore::Name(situation);  // copied: Remove leaves the reference stale
			const auto holders = Assignments::HoldersOf(situation).size();
			if (const auto error = SituationStore::Remove(situation)) {
				CaptureWindow::SetError(*error);
				return;
			}
			const auto wearing = Assignments::ForgetSituation(situation);
			Notify(holders > 0 ? std::format("Livery: removed the situation \"{}\"; cleared from {} character(s).", name, holders) :
								 std::format("Livery: removed the situation \"{}\".", name));
			const auto                                target = handle.get();
			std::optional<CaptureWindow::ActorPieces> redressed;
			for (const auto id : wearing) {
				if (id == actorID && target) {
					redressed = RedressCaptured(*target, {}, armors, offered, nullptr);
				} else {
					QueueRedress(id);
				}
			}
			auto list = CurrentOutfitList(actorID, armors);
			list.actorPieces = std::move(redressed);
			CaptureWindow::SetOutfits(std::move(list));
		};
		// Nobody's rows change: a restored situation comes back empty in this save.
		request.onRestorePresets = [actorID, armors] {
			if (const auto error = SituationStore::RestorePresets()) {
				CaptureWindow::SetError(*error);
				return;
			}
			Notify("Livery: restored the preset situations.");
			CaptureWindow::SetOutfits(CurrentOutfitList(actorID, armors));
		};
	}

	// The player's armor under Player's Inventory, for an NPC: the forms the NPC does
	// not carry; the others are listed once, under the NPC's own headings. Returns the armor
	// it added.
	// Contract: game thread; `armors` matches `pieces` index for index.
	std::vector<RE::TESObjectARMO*> AddPlayerPieces(RE::Actor& actor, std::vector<RE::TESObjectARMO*>& armors, std::vector<CaptureWindow::Piece>& pieces)
	{
		std::vector<RE::TESObjectARMO*> added;
		auto*                           player = RE::PlayerCharacter::GetSingleton();
		if (!player || actor.IsPlayerRef()) {
			return added;
		}
		for (auto* armor : OfferedPlayerArmor(*player)) {
			if (std::ranges::find(armors, armor) == armors.end()) {
				pieces.push_back(MakePiece(*armor, CaptureWindow::PieceGroup::kPlayer, false));
				armors.push_back(armor);
				added.push_back(armor);
			}
		}
		return added;
	}

	// Contract: game thread (library reads that hand out armor forms run here, so
	// the window itself never calls the library).
	// Every inventory piece is listed, the worn ones under the library outfit Livery put on
	// them, Livery's pieces that are not one, or those Livery did not put on; worn ones start
	// ticked. For an NPC the player's armor follows (AddPlayerPieces), then the library
	// outfits' other pieces, for Load Outfit.
	void OpenNameWindow(RE::Actor& actor, const std::vector<InventoryArmor>& inventory)
	{
		CaptureWindow::Request request;
		request.actorName = actor.GetName() ? actor.GetName() : "NPC";
		if (const auto* player = RE::PlayerCharacter::GetSingleton(); player && player->GetName()) {
			request.playerName = player->GetName();
		}
		request.suggestedName = NextAutomaticName();
		request.categories = CategoryOptions();
		const auto  choices = Assignments::ChoicesOf(actor.GetFormID());
		const auto* livery = WornLibraryOutfit(choices);
		if (livery) {
			request.outfitName = livery->name;
			request.wornOutfit = livery->id;
			request.wornFor = choices->worn;
			if (choices->worn != Situations::Situation::kDefault) {
				request.wornSituation = SituationStore::Name(choices->worn);
			}
		}
		g_capturedGaps = GapsOf(choices, inventory);  // read before the preview begins
		std::vector<RE::TESObjectARMO*> armors;
		armors.reserve(inventory.size());
		for (const auto& piece : inventory) {
			request.pieces.push_back(MakePiece(*piece.armor, OwnPieceGroup(piece, choices, livery), StartsTicked(piece)));
			armors.push_back(piece.armor);
		}
		const auto offered = AddPlayerPieces(actor, armors, request.pieces);
		request.existingOutfits = ExistingOutfits(armors, request.pieces);
		AttachAssignment(request, actor, armors, offered, choices);
		AttachSave(request, actor, armors, choices);
		AttachPreview(request, actor, armors);
		AttachManage(request, actor, armors, offered);
		AttachSituations(request, actor, armors, offered);
		// Every close, whatever closed the window, ends the preview, redresses the actors
		// Manage Wardrobe or Manage Situations left for it, lets saving back on and lets an NPC
		// walk again.
		request.onClose = [] {
			PreviewCamera::End();
			OutfitPreview::End();
			RedressAtClose();
			AllowSaving();
			MovementHold::End();
			NpcReaction::End();
		};
		BlockSaving();
		MovementHold::Begin(actor);
		// A frozen game (Live Preview off) would hold the head turn and the greeting until the close.
		if (request.previewOn) {
			NpcReaction::Begin(actor);
		}
		CaptureWindow::Open(std::move(request));
	}

	// Contract: game thread (SKSE task queued by the hotkey sink).
	void Capture(RE::ObjectRefHandle handle)
	{
		if (!g_ready) {
			Notify("Livery is not ready.");
			return;
		}
		if (CaptureWindow::IsOpen()) {
			return;
		}
		// No overlay (Skyrim VR, or ImGui failed to initialise): the hotkey only opens the window.
		if (!Hooks::IsRenderReady()) {
			Notify("Livery: the window is not available.");
			return;
		}

		// Nothing (or something other than an actor) under the crosshair captures the player.
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const auto ref = handle.get();
		auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor) {
			actor = player;
		}
		if (!actor) {
			return;
		}
		const bool isPlayer = actor == player;
		// Livery never dresses children (Assignments refuses them too).
		if (actor->IsChild()) {
			Notify("Livery: children are not supported.");
			return;
		}
		// The follower restriction narrows which NPCs qualify; the player always does.
		if (!isPlayer && Settings::FollowersAndSpouseOnly() && !IsFollowerOrSpouse(*actor)) {
			Notify("Livery: followers and your spouse only (see Settings).");
			return;
		}

		std::size_t skipped = 0;
		std::size_t substituted = 0;
		// The window also offers carried armor and Load Outfit, so it opens even when
		// the actor carries no armor.
		OpenNameWindow(*actor, CollectArmor(*actor, skipped, substituted));
	}

	// The settings page's Release everyone, refused while a release could break what the
	// window is doing.
	// Contract: SKSE Menu Framework's render thread; the release runs as an SKSE task.
	void QueueReleaseEveryone()
	{
		SKSE::GetTaskInterface()->AddTask([] {
			if (!g_ready || CaptureWindow::IsOpen()) {
				Notify("Livery: load a save and close the window first.");
				return;
			}
			Assignments::ReleaseEveryone();
		});
	}

	// --- Event sinks ----------------------------------------------------------

	class CrosshairSink final : public RE::BSTEventSink<SKSE::CrosshairRefEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* event, RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override
		{
			g_target = event && event->crosshairRef ? event->crosshairRef->GetHandle() : RE::ObjectRefHandle{};
			return RE::BSEventNotifyControl::kContinue;
		}

		static CrosshairSink& Get()
		{
			static CrosshairSink sink;
			return sink;
		}
	};

	class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!events || !*events || !g_ready || RE::UI::GetSingleton()->GameIsPaused()) {
				return RE::BSEventNotifyControl::kContinue;
			}

			for (auto* event = *events; event; event = event->next) {
				auto* button = event->AsButtonEvent();
				if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) {
					continue;
				}
				if (Settings::IsHotkey(button->GetIDCode())) {
					const auto handle = g_target;
					SKSE::GetTaskInterface()->AddTask([handle] { Capture(handle); });
					break;
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static InputSink& Get()
		{
			static InputSink sink;
			return sink;
		}
	};

	// --- Messaging ------------------------------------------------------------

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		switch (message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			Settings::RegisterMenu(QueueReleaseEveryone);
			break;

		case SKSE::MessagingInterface::kInputLoaded:
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&InputSink::Get());
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			SituationStore::Load();  // before any co-save load, which drops the rows of removed situations
			Library::Load();         // pieces resolve against the loaded plugins, so not before this
			PieceIcons::Load();      // its rule files go by the loaded plugins' names
			SKSE::GetCrosshairRefEventSource()->AddEventSink(&CrosshairSink::Get());
			Assignments::RegisterSinks();
			Hooks::RegisterMenu();
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
			g_ready = false;
			g_target = {};
			Situations::Stop();
			// The world the preview was set in is being replaced, inventories included: drop
			// the preview without dressing anyone.
			OutfitPreview::Abandon();
			g_redressAtClose.clear();  // its actor is going away with the world
			CaptureWindow::Close();    // discard a pending capture; its actor is going away
			break;

		case SKSE::MessagingInterface::kNewGame:
			OutfitPreview::Abandon();  // a new game replaces the world without a kPreLoadGame
			[[fallthrough]];
		case SKSE::MessagingInterface::kPostLoadGame:
			g_ready = true;
			g_target = {};
			Situations::Start();
			break;

		default:
			break;
		}
	}

	// --- Entry ----------------------------------------------------------------

	void SetupLog()
	{
		const auto dir = logger::log_directory();
		if (!dir) {
			return;
		}
		const auto path = *dir / "Livery.log";
		auto       sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto       log = std::make_shared<spdlog::logger>("global", sink);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_level(spdlog::level::info);
		spdlog::flush_on(spdlog::level::info);
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	// Five write_call<5> hooks (two in Hooks.cpp, three in TradeFilter.cpp) need 14 bytes each;
	// 128 leaves room to grow.
	SKSE::Init(skse, { .trampoline = true, .trampolineSize = 128 });
	SetupLog();
	logger::info("Livery loaded");
	Settings::Load();
	Assignments::RegisterCoSave();
	Hooks::Install();
	TradeFilter::Install();
	return SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
}
