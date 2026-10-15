// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants      - hotkey, Tailor's built-in categories
//   Tailor writes  - SaveOutfit creates the outfit, adds pieces, then the chosen categories;
//                    UpdateOutfit replaces a confirmed existing outfit's pieces (add first, then remove)
//   Capture        - F8 handler: check the target, collect worn armor, detect the armor class, list the
//                    library's outfits, then open the window with it ticked (or save under an automatic
//                    name with it where the overlay is unavailable)
//   Event sinks    - crosshair target, hotkey
//   Messaging      - Tailor interface, settings page, sink and menu registration, ready state
//   Entry          - log, settings, trampoline, hooks
#include "PCH.h"

#include "CaptureWindow.h"
#include "Hooks.h"
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
#include <vector>

namespace
{
	constexpr std::uint32_t kHotkey = 0x42;  // F8 (DirectInput scan code)

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

	// Worn armor as plugin-defined forms. AddArmorToOutfit refuses runtime-created
	// armor (FormID 0xFF......), so player-enchanted gear is replaced by the plugin
	// armor it was made from (templateArmor, TNAM) and counted in substituted;
	// runtime-created armor without one is left out and counted in skipped.
	std::vector<RE::TESObjectARMO*> CollectWornArmor(RE::Actor& actor, std::size_t& skipped, std::size_t& substituted)
	{
		const auto inventory = actor.GetInventory([](RE::TESBoundObject& item) { return item.IsArmor(); });

		std::vector<RE::TESObjectARMO*> armors;
		armors.reserve(inventory.size());
		skipped = 0;
		substituted = 0;

		for (const auto& [object, itemData] : inventory) {
			const auto& [count, entry] = itemData;
			if (count <= 0 || !entry || !entry->IsWorn()) {
				continue;
			}
			auto* armor = object->As<RE::TESObjectARMO>();
			if (!armor) {
				continue;
			}
			if (armor->IsDynamicForm()) {
				auto* base = armor->templateArmor;
				if (!base || base->IsDynamicForm()) {
					++skipped;
					logger::info("Skipped runtime-created armor {:08X}", armor->GetFormID());
					continue;
				}
				++substituted;
				logger::info("Using base armor {:08X} for runtime-created armor {:08X}", base->GetFormID(), armor->GetFormID());
				armor = base;
			}
			if (std::ranges::find(armors, armor) == armors.end()) {
				armors.push_back(armor);
			}
		}
		return armors;
	}

	std::string DisplayName(const RE::TESObjectARMO& armor)
	{
		const auto* name = armor.GetName();
		return name && *name ? name : std::format("{:08X}", armor.GetFormID());
	}

	// Wigs occupy the Hair slot without the Head slot (helmets and most hats
	// cover both). They are offered unticked so a captured outfit does not
	// replace another character's hair by default.
	bool LooksLikeWig(const RE::TESObjectARMO& armor)
	{
		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		return armor.HasPartOf(Slot::kHair) && !armor.HasPartOf(Slot::kHead);
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
			if (!g_tailor->DoesOutfitExist(name.c_str())) {
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
		auto        worn = CollectWornArmor(actor, skipped, substituted);
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
	void OpenNameWindow(RE::Actor& actor, std::vector<RE::TESObjectARMO*> armors)
	{
		CaptureWindow::Request request;
		request.actorName = actor.GetName() ? actor.GetName() : "NPC";
		request.actorSummary = DescribeActor(actor);
		request.suggestedName = NextAutomaticName();
		request.categories = AvailableCategories();
		request.existingOutfits = ExistingOutfits(request.categories);
		const auto* armorClass = DetectArmorClass(armors);
		for (auto& category : request.categories) {
			category.selected = category.name == armorClass;
		}
		request.suggestedGender = DetectGender(armors);
		for (const auto* armor : armors) {
			request.pieces.push_back({ DisplayName(*armor), !LooksLikeWig(*armor) });
		}
		request.onSave = [armors](const std::string& name, const std::vector<std::size_t>& pieces, const std::vector<std::string>& categories,
							 int gender) {
			if (const auto error = SaveOutfit(name, PickPieces(armors, pieces), categories, gender)) {
				CaptureWindow::SetError(*error);
			} else {
				CaptureWindow::Close();
			}
		};
		request.onUpdate = [armors = std::move(armors)](const std::string& name, const std::vector<std::size_t>& pieces,
							   const std::vector<std::string>& categories) {
			if (const auto error = UpdateOutfit(name, PickPieces(armors, pieces), categories)) {
				CaptureWindow::SetError(*error);
			} else {
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

		const auto ref = handle.get();
		auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor || actor == RE::PlayerCharacter::GetSingleton()) {
			Notify("Tailor Capture: aim at an NPC.");
			return;
		}
		// Tailor never handles children (see TailorAPI.h).
		if (actor->IsChild()) {
			Notify("Tailor Capture: children's outfits cannot be captured.");
			return;
		}
		if (Settings::FollowersAndSpouseOnly() && !IsFollowerOrSpouse(*actor)) {
			Notify("Tailor Capture: only followers and your spouse can be captured.");
			return;
		}

		std::size_t skipped = 0;
		std::size_t substituted = 0;
		auto        armors = CollectWornArmor(*actor, skipped, substituted);
		if (armors.empty()) {
			Notify(skipped ? "Tailor Capture: worn armor was created in-game and cannot be saved." :
							 "Tailor Capture: no worn armor was found.");
			return;
		}

		if (Hooks::IsRenderReady()) {
			OpenNameWindow(*actor, std::move(armors));
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
				if (button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard && button->GetIDCode() == kHotkey && button->IsDown()) {
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
