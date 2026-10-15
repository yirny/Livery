// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants      - Tailor's built-in categories
//   Tailor writes  - SaveOutfit creates the outfit, adds pieces, then the chosen categories;
//                    UpdateOutfit replaces a confirmed existing outfit's pieces (add first, then remove)
//   Preview        - the captured actor wears kPreviewOutfit as an override that follows the piece ticks,
//                    seen through PreviewCamera; closing the window ends both, a save moves the override
//                    to the saved outfit
//   Capture        - hotkey handler: check the target (the player when nothing is aimed at), collect inventory
//                    armor, detect the armor class, list the library's outfits, then open the window with the
//                    worn pieces ticked (or save those under an automatic name where the overlay is unavailable)
//   Event sinks    - crosshair target, hotkey
//   Messaging      - Tailor interface, settings page, sink and menu registration, ready state
//   Entry          - log, settings, trampoline, hooks
#include "PCH.h"

#include "CaptureWindow.h"
#include "Hooks.h"
#include "PreviewCamera.h"
#include "Settings.h"
#include "TailorAPI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	// Tailor's built-in categories (TailorAPI.h, "RULES EVERY FUNCTION SHARES").
	// API 1.0 cannot list categories; the player's custom ones come from
	// Tailor's library.json instead (ReadCustomCategoryNames).
	struct BuiltInCategory
	{
		const char* group;
		const char* name;
	};
	constexpr std::array<BuiltInCategory, 9> kBuiltInCategories{ {
		{ "Armor class", "Clothing" },
		{ "Armor class", "Light Armor" },
		{ "Armor class", "Heavy Armor" },
		{ "Situation", "Adventuring" },
		{ "Situation", "Town" },
		{ "Situation", "Home" },
		{ "Situation", "Sleep" },
		{ "Situation", "Swimming" },
		{ "Situation", "Warm" },
	} };

	// Slots that decide an outfit's armor class and gender, most telling first:
	// Body sets how the outfit reads at a glance.
	using BipedSlot = RE::BGSBipedObjectForm::BipedObjectSlot;
	constexpr std::array kSlotPriority{ BipedSlot::kBody, BipedSlot::kHead, BipedSlot::kHands, BipedSlot::kFeet, BipedSlot::kCirclet };

	// The Tailor outfit the live preview dresses the captured actor in. Created
	// once and reused (API 1.0 cannot delete it); never offered as an outfit to update.
	constexpr const char* kPreviewOutfit = "Capture Preview";

	TailorAPI::ITailorInterface1* g_tailor = nullptr;
	RE::ObjectRefHandle           g_target{};
	bool                          g_ready = false;

	void Notify(const std::string& text)
	{
		RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		logger::info("{}", text);
	}

	// --- Tailor writes --------------------------------------------------------

	// First "Captured Outfit NNN" that does not exist yet, or "" when the
	// library is full of them.
	std::string NextAutomaticName()
	{
		for (int index = 1; index <= 99999; ++index) {
			auto name = std::format("Captured Outfit {:03}", index);
			if (!g_tailor->DoesOutfitExist(name.c_str())) {
				return name;
			}
		}
		return {};
	}

	// Contract: Tailor's JSON files are its own, not part of its API: read only,
	// never written. They can lag behind Tailor's in-memory library, so callers
	// keep only the names the API confirms. Any surprise yields a discarded value.
	nlohmann::json ReadTailorFile(const char* a_file, int a_knownVersion)
	{
		const auto    path = std::filesystem::current_path() / "Data/SKSE/Plugins/Tailor" / a_file;
		std::ifstream file{ path };
		if (!file) {
			logger::warn("Cannot open Tailor's {}", a_file);
			return nlohmann::json::value_t::discarded;
		}
		auto json = nlohmann::json::parse(file, nullptr, false);
		if (json.is_discarded() || !json.is_object()) {
			logger::warn("Tailor's {} is not valid JSON", a_file);
			return nlohmann::json::value_t::discarded;
		}
		const auto version = json.find("version");
		if (version == json.end() || !version->is_number_integer() || version->get<int>() != a_knownVersion) {
			logger::warn("Tailor's {} has an unknown version", a_file);
			return nlohmann::json::value_t::discarded;
		}
		return json;
	}

	std::vector<std::string> ReadCustomCategoryNames()
	{
		constexpr int kKnownVersion = 2;  // the "version" field this reader was written against

		const auto library = ReadTailorFile("library.json", kKnownVersion);
		if (library.is_discarded()) {
			return {};
		}
		const auto list = library.find("categories");
		if (list == library.end() || !list->is_array()) {
			logger::warn("Tailor's library.json has no category list");
			return {};
		}

		std::vector<std::string> names;
		for (const auto& category : *list) {
			const auto isDefault = category.find("isDefault");
			const auto name = category.find("name");
			if (isDefault != category.end() && isDefault->is_boolean() && !isDefault->get<bool>() &&
				name != category.end() && name->is_string()) {
				names.push_back(name->get<std::string>());
			}
		}
		return names;
	}

	std::vector<std::string> ReadOutfitNames()
	{
		constexpr int kKnownVersion = 1;  // the "version" field this reader was written against

		const auto outfits = ReadTailorFile("outfits.json", kKnownVersion);
		if (outfits.is_discarded()) {
			return {};
		}
		const auto list = outfits.find("outfits");
		if (list == outfits.end() || !list->is_array()) {
			logger::warn("Tailor's outfits.json has no outfit list");
			return {};
		}

		std::vector<std::string> names;
		for (const auto& outfit : *list) {
			const auto name = outfit.find("name");
			if (name != outfit.end() && name->is_string()) {
				names.push_back(name->get<std::string>());
			}
		}
		return names;
	}

	std::vector<CaptureWindow::CategoryOption> AvailableCategories()
	{
		std::vector<CaptureWindow::CategoryOption> categories;
		for (const auto& category : kBuiltInCategories) {
			if (g_tailor->DoesCategoryExist(category.name)) {
				categories.push_back({ category.group, category.name });
			}
		}
		for (auto& name : ReadCustomCategoryNames()) {
			if (g_tailor->DoesCategoryExist(name.c_str())) {
				categories.push_back({ "Custom", std::move(name) });
			}
		}
		return categories;
	}

	// Adds the outfit to each category; returns the ones Tailor refused, joined
	// for a HUD message ("" when none).
	// Contract: game thread; call only once the outfit has pieces (an empty
	// outfit in a situation pool can dress someone in nothing). API 1.0 cannot
	// take an outfit out of a category again.
	std::string AddCategories(const std::string& name, const std::vector<std::string>& categories)
	{
		std::string failed;
		for (const auto& category : categories) {
			if (!g_tailor->AddCategory(name.c_str(), category.c_str())) {
				logger::warn("AddCategory({}, {}) failed", name, category);
				failed += failed.empty() ? category : ", " + category;
			}
		}
		return failed;
	}

	// Contract: game thread; Tailor 3.0 library already loaded (g_ready).
	// API 1.0 cannot delete an outfit, so every refusal that can be checked up
	// front is checked before CreateOutfit. Categories are added only after the
	// pieces: an empty outfit in a situation pool can dress someone in nothing.
	// gender is Tailor's value (-1 unisex, 0 male, 1 female); API 1.0 cannot change it later.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> SaveOutfit(const std::string& name, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<std::string>& categories, int gender)
	{
		if (!g_ready || !g_tailor) {
			return "Tailor 3.0+ is not ready.";
		}
		if (g_tailor->DoesOutfitExist(name.c_str())) {
			return std::format("An outfit named \"{}\" already exists.", name);
		}
		if (!g_tailor->CreateOutfit(name.c_str(), "", gender)) {
			return "Tailor refused to create the outfit (see Tailor.log).";
		}

		std::size_t added = 0;
		for (auto* armor : armors) {
			if (g_tailor->AddArmorToOutfit(name.c_str(), armor)) {
				++added;
			} else {
				logger::warn("Rejected armor {:08X}", armor->GetFormID());
			}
		}
		if (!added) {
			// Nothing can be undone here; tell the user what to clean up.
			return "Tailor rejected every piece; delete the empty outfit in Tailor.";
		}
		if (const auto failed = AddCategories(name, categories); !failed.empty()) {
			Notify(std::format("Tailor: saved {} ({} pieces), but could not add it to: {}.", name, added, failed));
			return std::nullopt;
		}

		Notify(std::format("Tailor: saved {} ({} pieces).", name, added));
		if (added != armors.size()) {
			logger::warn("Saved {} of {} worn pieces", added, armors.size());
		}
		return std::nullopt;
	}

	// The outfit's loaded pieces; pieces from plugins that are not loaded are left out.
	std::vector<RE::TESObjectARMO*> OutfitArmors(const std::string& name)
	{
		std::vector<RE::TESObjectARMO*> armors;
		g_tailor->GetOutfitArmors(
			name.c_str(), [](RE::TESObjectARMO* armor, void* context) {
				if (armor) {
					static_cast<std::vector<RE::TESObjectARMO*>*>(context)->push_back(armor);
				}
			},
			&armors);
		return armors;
	}

	// Contract: game thread; Tailor 3.0 library already loaded (g_ready); the user
	// confirmed replacing this outfit's pieces. New pieces go in before old ones
	// come out, and nothing comes out unless one went in, so the outfit (which may
	// sit in a situation pool) is never left empty. The confirmed `categories`
	// are added only after a piece went in; its other categories and gender stay.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> UpdateOutfit(const std::string& name, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<std::string>& categories)
	{
		if (!g_ready || !g_tailor) {
			return "Tailor 3.0+ is not ready.";
		}
		if (!g_tailor->DoesOutfitExist(name.c_str())) {
			return std::format("The outfit \"{}\" no longer exists.", name);
		}
		const auto previous = OutfitArmors(name);

		std::vector<RE::TESObjectARMO*> accepted;
		for (auto* armor : armors) {
			if (g_tailor->AddArmorToOutfit(name.c_str(), armor)) {
				accepted.push_back(armor);
			} else {
				logger::warn("Rejected armor {:08X}", armor->GetFormID());
			}
		}
		if (accepted.empty()) {
			return std::format("Tailor rejected every piece; \"{}\" was not changed.", name);
		}

		std::size_t stuck = 0;
		for (auto* armor : previous) {
			if (std::ranges::find(accepted, armor) == accepted.end() && !g_tailor->RemoveArmorFromOutfit(name.c_str(), armor)) {
				logger::warn("RemoveArmorFromOutfit({}, {:08X}) failed", name, armor->GetFormID());
				++stuck;
			}
		}
		std::string problems;
		if (stuck) {
			problems = std::format("{} old piece(s) could not be removed", stuck);
		}
		if (const auto failed = AddCategories(name, categories); !failed.empty()) {
			problems += (problems.empty() ? "" : "; ") + std::format("could not add it to: {}", failed);
		}
		if (!problems.empty()) {
			Notify(std::format("Tailor: updated {} ({} pieces), but {}.", name, accepted.size(), problems));
			return std::nullopt;
		}

		Notify(std::format("Tailor: updated {} ({} pieces).", name, accepted.size()));
		if (accepted.size() != armors.size()) {
			logger::warn("Updated with {} of {} ticked pieces", accepted.size(), armors.size());
		}
		return std::nullopt;
	}

	// --- Preview --------------------------------------------------------------

	// The actor the open window previews on, wearing kPreviewOutfit as an
	// override; empty when there is no live preview.
	RE::ActorHandle g_previewActor{};

	// Gives kPreviewOutfit exactly these pieces, new ones in before old ones out.
	// Contract: game thread (Tailor building calls).
	void SetPreviewPieces(const std::vector<RE::TESObjectARMO*>& armors)
	{
		const auto previous = OutfitArmors(kPreviewOutfit);
		for (auto* armor : armors) {
			if (!g_tailor->AddArmorToOutfit(kPreviewOutfit, armor)) {
				logger::warn("Preview rejected armor {:08X}", armor->GetFormID());
			}
		}
		for (auto* armor : previous) {
			if (std::ranges::find(armors, armor) == armors.end() && !g_tailor->RemoveArmorFromOutfit(kPreviewOutfit, armor)) {
				logger::warn("RemoveArmorFromOutfit({}, {:08X}) failed", kPreviewOutfit, armor->GetFormID());
			}
		}
	}

	// Dresses the actor in kPreviewOutfit holding `armors` until EndPreview or
	// DressInSavedOutfit. Returns "" when the preview is on, or why it is off.
	// Contract: game thread. An override already set (by another mod, or in
	// Tailor) is never replaced: API 1.0 cannot read it back to restore it.
	// kPreviewOutfit is the only outfit changed before the user confirms: it is
	// created with no category, so Tailor never picks it at random, even empty.
	std::string BeginPreview(RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& armors)
	{
		if (g_tailor->HasOutfitOverride(&actor)) {
			return "Preview off: an outfit override is set.";
		}
		if (!g_tailor->DoesOutfitExist(kPreviewOutfit) && !g_tailor->CreateOutfit(kPreviewOutfit, "", -1)) {
			return "Preview off: Tailor refused to create its outfit.";
		}
		SetPreviewPieces(armors);
		if (!g_tailor->OverrideWithOutfit(&actor, kPreviewOutfit)) {
			return "Preview off: Tailor refused to dress them.";
		}
		g_previewActor = actor.GetHandle();
		logger::info("Preview on {:08X}", actor.GetFormID());
		return {};
	}

	// Tailor dresses everyone wearing kPreviewOutfit again after each change.
	// Contract: game thread (SKSE task queued by the window, in click order).
	void SetPreviewPiece(RE::TESObjectARMO* armor, bool on)
	{
		if (!g_previewActor) {
			return;
		}
		const bool done = on ? g_tailor->AddArmorToOutfit(kPreviewOutfit, armor) : g_tailor->RemoveArmorFromOutfit(kPreviewOutfit, armor);
		if (!done) {
			logger::warn("Preview could not {} armor {:08X}", on ? "add" : "remove", armor->GetFormID());
		}
	}

	// The actor goes back to what Tailor would dress them in without an override.
	// Contract: game thread; the window's onClose, so it also runs after a save,
	// by when DressInSavedOutfit has already ended the preview.
	void EndPreview()
	{
		if (const auto actor = std::exchange(g_previewActor, {}).get()) {
			g_tailor->ClearOutfitOverride(actor.get());
		}
	}

	// Moves the preview's override from kPreviewOutfit to the outfit just saved;
	// it stays on until cleared in Tailor's screens. An outfit that does not fit
	// them (its gender) ends the override instead.
	// Contract: game thread, after SaveOutfit / UpdateOutfit succeeded.
	void DressInSavedOutfit(const std::string& name)
	{
		const auto actor = std::exchange(g_previewActor, {}).get();
		if (!actor) {
			return;
		}
		if (!g_tailor->OverrideWithOutfit(actor.get(), name.c_str())) {
			g_tailor->ClearOutfitOverride(actor.get());
			Notify(std::format("Tailor Capture: could not dress them in {}.", name));
		}
	}

	// Starts the live preview for the window about to open and wires it in: ticks
	// change kPreviewOutfit, the mouse over the game turns PreviewCamera (when it
	// could take the camera), and closing the window ends both. When the preview
	// cannot start, the window says why.
	// Contract: game thread; `armors` matches request.pieces index for index.
	void AttachPreview(CaptureWindow::Request& request, RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& armors)
	{
		std::vector<RE::TESObjectARMO*> ticked;
		for (std::size_t i = 0; i < armors.size(); ++i) {
			if (request.pieces[i].selected) {
				ticked.push_back(armors[i]);
			}
		}
		request.previewNote = BeginPreview(actor, ticked);
		if (!request.previewNote.empty()) {
			return;
		}
		request.onPieceToggled = [armors](std::size_t index, bool on) { SetPreviewPiece(armors[index], on); };
		if (PreviewCamera::Begin(actor)) {
			request.onViewInput = PreviewCamera::Orbit;
		}
		request.onClose = [] {
			PreviewCamera::End();
			EndPreview();
		};
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

	// Inventory armor as plugin-defined forms, worn pieces first, each group in
	// name order. AddArmorToOutfit refuses runtime-created armor (FormID 0xFF......),
	// so player-enchanted gear is replaced by the plugin armor it was made from
	// (templateArmor, TNAM); runtime-created armor without one is left out. Only
	// worn pieces count in substituted and skipped. A form both worn and carried
	// is listed once, as worn.
	// Non-playable armor is left out uncounted: body-mod parts such as the BHUNP /
	// 3BA SMP objects (slot 48/50/51/60 per their MCM) are not clothing, and Tailor
	// itself leaves non-playable armor on when it dresses the player.
	std::vector<InventoryArmor> CollectArmor(RE::Actor& actor, std::size_t& skipped, std::size_t& substituted)
	{
		const auto inventory = actor.GetInventory([](RE::TESBoundObject& item) { return item.IsArmor(); });

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
			if (armor->IsDynamicForm()) {
				auto* base = armor->templateArmor;
				if (!base || base->IsDynamicForm()) {
					if (worn) {
						++skipped;
						logger::info("Skipped runtime-created armor {:08X}", armor->GetFormID());
					}
					continue;
				}
				if (worn) {
					++substituted;
					logger::info("Using base armor {:08X} for runtime-created armor {:08X}", base->GetFormID(), armor->GetFormID());
				}
				armor = base;
			}
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

	// Tailor armor-type category of an outfit: the armor type of the first piece
	// found on a kSlotPriority slot; Clothing when none is.
	const char* DetectArmorClass(const std::vector<RE::TESObjectARMO*>& armors)
	{
		using Type = RE::BGSBipedObjectForm::ArmorType;

		for (const auto slot : kSlotPriority) {
			for (const auto* armor : armors) {
				if (!armor->HasPartOf(slot)) {
					continue;
				}
				switch (armor->GetArmorType()) {
				case Type::kLightArmor:
					return "Light Armor";
				case Type::kHeavyArmor:
					return "Heavy Armor";
				case Type::kClothing:
					return "Clothing";
				default:
					break;  // out-of-range value from a broken record: try the next piece
				}
			}
		}
		return "Clothing";
	}

	// A piece is for women when none of its armor addons has a male model and
	// one has a female model.
	bool IsFemaleOnly(const RE::TESObjectARMO& armor)
	{
		bool female = false;
		for (const auto* addon : armor.armorAddons) {
			if (!addon) {
				continue;
			}
			const auto* male = addon->bipedModels[RE::SEXES::kMale].GetModel();
			if (male && *male) {
				return false;
			}
			const auto* model = addon->bipedModels[RE::SEXES::kFemale].GetModel();
			female = female || (model && *model);
		}
		return female;
	}

	// Tailor gender of an outfit (-1 unisex, 1 female): decided by the first
	// piece found on a kSlotPriority slot; unisex when none is. Never male: only
	// the user picks that, in the window.
	int DetectGender(const std::vector<RE::TESObjectARMO*>& armors)
	{
		for (const auto slot : kSlotPriority) {
			for (const auto* armor : armors) {
				if (armor->HasPartOf(slot)) {
					return IsFemaleOnly(*armor) ? 1 : -1;
				}
			}
		}
		return -1;
	}

	// The library's outfits in case-insensitive name order, each with the known
	// categories it is in and its loaded pieces. Outfit names come from
	// outfits.json (API 1.0 cannot list every outfit), checked with DoesOutfitExist.
	std::vector<CaptureWindow::ExistingOutfit> ExistingOutfits(const std::vector<CaptureWindow::CategoryOption>& categories)
	{
		auto names = ReadOutfitNames();
		std::ranges::sort(names, [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });

		std::vector<CaptureWindow::ExistingOutfit> outfits;
		for (auto& name : names) {
			if (_stricmp(name.c_str(), kPreviewOutfit) == 0 || !g_tailor->DoesOutfitExist(name.c_str())) {
				continue;
			}
			CaptureWindow::ExistingOutfit outfit{ .name = std::move(name) };
			for (const auto& category : categories) {
				if (g_tailor->IsOutfitInCategory(outfit.name.c_str(), category.name.c_str())) {
					outfit.categories.push_back(category.name);
				}
			}
			for (const auto* armor : OutfitArmors(outfit.name)) {
				outfit.pieces.push_back(DisplayName(*armor));
			}
			outfits.push_back(std::move(outfit));
		}
		return outfits;
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

	void CopyTailorString(const char* value, void* context)
	{
		*static_cast<std::string*>(context) = value ? value : "";
	}

	// Whether the worn armor is exactly the outfit's loaded pieces (e.g. not after
	// the player handed the NPC better gear). Runtime-created armor never belongs
	// to an outfit, so wearing any counts as a change, even when its base armor
	// matches a piece.
	// Contract: game thread (reads the actor's inventory).
	bool WearsOutfitAsIs(RE::Actor& actor, const std::string& outfit)
	{
		std::size_t skipped = 0;
		std::size_t substituted = 0;
		auto        worn = WornArmors(CollectArmor(actor, skipped, substituted));
		auto        pieces = OutfitArmors(outfit);
		std::ranges::sort(worn);
		std::ranges::sort(pieces);
		const auto duplicates = std::ranges::unique(pieces);
		pieces.erase(duplicates.begin(), duplicates.end());
		return skipped == 0 && substituted == 0 && worn == pieces;
	}

	// The line under the actor's name: where Tailor judges them to be and what it
	// put on them. A fight counts as Adventuring: Tailor dresses people with an
	// Adventuring outfit for it, while GetSituation keeps naming the place.
	// Contract: game thread (GetOutfit and GetSituation take an actor). Tailor's
	// strings are valid only during the callback, so CopyTailorString copies them.
	std::string DescribeActor(RE::Actor& actor)
	{
		struct SituationPhrase
		{
			const char* situation;  // as GetSituation names it
			const char* phrase;     // keeps the word Tailor shows for it
		};
		constexpr std::array<SituationPhrase, 6> kSituationPhrases{ {
			{ "adventuring", "Out Adventuring" },
			{ "town", "In Town" },
			{ "home", "At Home" },
			{ "sleep", "Ready for Sleep" },
			{ "swimming", "Swimming" },
			{ "warm", "Keeping Warm" },
		} };

		std::string outfit;
		g_tailor->GetOutfit(&actor, CopyTailorString, &outfit);
		std::string situation = "adventuring";
		if (!actor.IsInCombat()) {
			situation.clear();
			g_tailor->GetSituation(&actor, CopyTailorString, &situation);
		}

		std::string wearing = "wearing an unmanaged outfit";
		if (!outfit.empty()) {
			wearing = std::format("wearing \"{}\"", outfit);
			if (!WearsOutfitAsIs(actor, outfit)) {
				wearing += " with changes";
			}
		}
		for (const auto& [name, phrase] : kSituationPhrases) {
			if (situation == name) {
				return std::format("{}, {}.", phrase, wearing);
			}
		}
		return std::format("W{}.", wearing.substr(1));
	}

	// Contract: game thread (Tailor queries that hand out armor forms run here,
	// so the window itself never calls Tailor).
	// Every inventory piece is listed; worn ones start ticked and decide the
	// suggested armor class and gender.
	void OpenNameWindow(RE::Actor& actor, const std::vector<InventoryArmor>& inventory)
	{
		CaptureWindow::Request request;
		request.actorName = actor.GetName() ? actor.GetName() : "NPC";
		request.actorSummary = DescribeActor(actor);
		request.suggestedName = NextAutomaticName();
		request.categories = AvailableCategories();
		request.existingOutfits = ExistingOutfits(request.categories);
		const auto  worn = WornArmors(inventory);
		const auto* armorClass = DetectArmorClass(worn);
		for (auto& category : request.categories) {
			category.selected = category.name == armorClass;
		}
		request.suggestedGender = DetectGender(worn);
		std::vector<RE::TESObjectARMO*> armors;
		armors.reserve(inventory.size());
		for (const auto& [armor, isWorn] : inventory) {
			request.pieces.push_back({ DisplayName(*armor), isWorn && !LooksLikeWig(*armor) });
			armors.push_back(armor);
		}
		AttachPreview(request, actor, armors);
		request.onSave = [armors](const std::string& name, const std::vector<std::size_t>& pieces, const std::vector<std::string>& categories,
							 int gender) {
			if (const auto error = SaveOutfit(name, PickPieces(armors, pieces), categories, gender)) {
				CaptureWindow::SetError(*error);
			} else {
				DressInSavedOutfit(name);
				CaptureWindow::Close();
			}
		};
		request.onUpdate = [armors = std::move(armors)](const std::string& name, const std::vector<std::size_t>& pieces,
							   const std::vector<std::string>& categories) {
			if (const auto error = UpdateOutfit(name, PickPieces(armors, pieces), categories)) {
				CaptureWindow::SetError(*error);
			} else {
				DressInSavedOutfit(name);
				CaptureWindow::Close();
			}
		};
		CaptureWindow::Open(std::move(request));
	}

	// Contract: game thread (SKSE task queued by the hotkey sink).
	void Capture(RE::ObjectRefHandle handle)
	{
		if (!g_ready || !g_tailor) {
			Notify("Tailor Capture: Tailor 3.0+ is not ready.");
			return;
		}
		if (CaptureWindow::IsOpen()) {
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
		// Tailor never handles children (see TailorAPI.h).
		if (actor->IsChild()) {
			Notify("Tailor Capture: children's outfits cannot be captured.");
			return;
		}
		// The follower restriction narrows which NPCs qualify; the player always does.
		if (!isPlayer && Settings::FollowersAndSpouseOnly() && !IsFollowerOrSpouse(*actor)) {
			Notify("Tailor Capture: only followers and your spouse can be captured.");
			return;
		}

		std::size_t skipped = 0;
		std::size_t substituted = 0;
		const auto  inventory = CollectArmor(*actor, skipped, substituted);
		const auto  armors = WornArmors(inventory);
		// The window also offers carried armor, so it opens with nothing worn.
		const bool hasWindow = Hooks::IsRenderReady();
		if (hasWindow ? inventory.empty() : armors.empty()) {
			Notify(skipped ? "Tailor Capture: worn armor was created in-game and cannot be saved." :
							 "Tailor Capture: no worn armor was found.");
			return;
		}

		if (hasWindow) {
			OpenNameWindow(*actor, inventory);
			return;
		}

		// No overlay (Skyrim VR, or ImGui failed to initialise): automatic name and the
		// detected armor class and gender. Never a situation pool: the user chose none here.
		const auto name = NextAutomaticName();
		if (name.empty()) {
			Notify("Tailor Capture: no free outfit name.");
			return;
		}
		std::vector<std::string> categories;
		if (const auto* armorClass = DetectArmorClass(armors); g_tailor->DoesCategoryExist(armorClass)) {
			categories.emplace_back(armorClass);
		}
		if (const auto error = SaveOutfit(name, armors, categories, DetectGender(armors))) {
			Notify("Tailor Capture: " + *error);
		}
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
				if (button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard && button->IsDown() && Settings::IsHotkey(button->GetIDCode())) {
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
			g_tailor = TailorAPI::RequestInterface1();
			logger::info("Tailor interface {}", g_tailor ? "found" : "not found");
			Settings::RegisterMenu();
			break;

		case SKSE::MessagingInterface::kInputLoaded:
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&InputSink::Get());
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			SKSE::GetCrosshairRefEventSource()->AddEventSink(&CrosshairSink::Get());
			Hooks::RegisterMenu();
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
			g_ready = false;
			g_target = {};
			// Forget the preview without asking Tailor: the world it was set in is being
			// replaced, and Tailor keeps overrides per save, so the save loading has none of it.
			g_previewActor = {};
			CaptureWindow::Close();  // discard a pending capture; its actor is going away
			break;

		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			g_ready = true;
			g_target = {};
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
		const auto path = *dir / "TailorOutfitCapture.log";
		auto       sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto       log = std::make_shared<spdlog::logger>("global", sink);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_level(spdlog::level::info);
		spdlog::flush_on(spdlog::level::info);
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	// Two write_call<5> hooks need 14 bytes each; 64 leaves room to grow.
	SKSE::Init(skse, { .trampoline = true, .trampolineSize = 64 });
	SetupLog();
	logger::info("TailorOutfitCapture loaded");
	Settings::Load();
	Hooks::Install();
	return SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
}
