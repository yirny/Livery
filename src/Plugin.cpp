// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants      - hotkey, Tailor's built-in categories
//   Tailor writes  - SaveOutfit creates the outfit, adds pieces, then the chosen categories
//   Capture        - F8 handler: check the target, collect worn armor, then open the name window
//                    (or save under an automatic name where the overlay is unavailable)
//   Event sinks    - crosshair target, hotkey
//   Messaging      - Tailor interface, settings page, sink registration, ready state
//   Entry          - log, settings, trampoline, hooks
#include "PCH.h"

#include "CaptureWindow.h"
#include "Hooks.h"
#include "Settings.h"
#include "TailorAPI.h"

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace
{
	constexpr std::uint32_t kHotkey = 0x42;  // F8 (DirectInput scan code)

	// Tailor's built-in categories (TailorAPI.h, "RULES EVERY FUNCTION SHARES").
	// API 1.0 cannot list categories, so the player's custom ones cannot be offered.
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

	std::vector<CaptureWindow::CategoryOption> AvailableCategories()
	{
		std::vector<CaptureWindow::CategoryOption> categories;
		for (const auto& category : kBuiltInCategories) {
			if (g_tailor->DoesCategoryExist(category.name)) {
				categories.push_back({ category.group, category.name });
			}
		}
		return categories;
	}

	// Contract: game thread; Tailor 3.0 library already loaded (g_ready).
	// API 1.0 cannot delete an outfit, so every refusal that can be checked up
	// front is checked before CreateOutfit. Categories are added only after the
	// pieces: an empty outfit in a situation pool can dress someone in nothing.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> SaveOutfit(const std::string& name, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<std::string>& categories)
	{
		if (!g_ready || !g_tailor) {
			return "Tailor 3.0+ is not ready.";
		}
		if (g_tailor->DoesOutfitExist(name.c_str())) {
			return std::format("An outfit named \"{}\" already exists.", name);
		}
		if (!g_tailor->CreateOutfit(name.c_str(), "", -1)) {
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
		std::string failed;
		for (const auto& category : categories) {
			if (!g_tailor->AddCategory(name.c_str(), category.c_str())) {
				logger::warn("AddCategory({}, {}) failed", name, category);
				failed += failed.empty() ? category : ", " + category;
			}
		}
		if (!failed.empty()) {
			Notify(std::format("Tailor: saved {} ({} pieces), but could not add it to: {}.", name, added, failed));
			return std::nullopt;
		}

		Notify(std::format("Tailor: saved {} ({} pieces).", name, added));
		if (added != armors.size()) {
			logger::warn("Saved {} of {} worn pieces", added, armors.size());
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

	// Worn, plugin-defined armor only. Runtime-created armor (FormID 0xFF......,
	// e.g. player-enchanted gear) is always refused by AddArmorToOutfit.
	std::vector<RE::TESObjectARMO*> CollectWornArmor(RE::Actor& actor, std::size_t& skipped)
	{
		const auto inventory = actor.GetInventory([](RE::TESBoundObject& item) { return item.IsArmor(); });

		std::vector<RE::TESObjectARMO*> armors;
		armors.reserve(inventory.size());
		skipped = 0;

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
				++skipped;
				logger::info("Skipped runtime-created armor {:08X}", armor->GetFormID());
				continue;
			}
			armors.push_back(armor);
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

	void OpenNameWindow(const RE::Actor& actor, std::vector<RE::TESObjectARMO*> armors)
	{
		CaptureWindow::Request request;
		request.actorName = actor.GetName() ? actor.GetName() : "NPC";
		request.suggestedName = NextAutomaticName();
		request.categories = AvailableCategories();
		for (const auto* armor : armors) {
			request.pieces.push_back({ DisplayName(*armor), !LooksLikeWig(*armor) });
		}
		request.onSave = [armors = std::move(armors)](const std::string& name, const std::vector<std::size_t>& pieces,
							 const std::vector<std::string>& categories) {
			std::vector<RE::TESObjectARMO*> selected;
			selected.reserve(pieces.size());
			for (const auto index : pieces) {
				selected.push_back(armors[index]);
			}
			if (const auto error = SaveOutfit(name, selected, categories)) {
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
		auto        armors = CollectWornArmor(*actor, skipped);
		if (armors.empty()) {
			Notify(skipped ? "Tailor Capture: worn armor was created in-game and cannot be saved." :
							 "Tailor Capture: no worn armor was found.");
			return;
		}

		if (Hooks::IsRenderReady()) {
			OpenNameWindow(*actor, std::move(armors));
			return;
		}

		// No overlay (Skyrim VR, or ImGui failed to initialise): automatic name, no category.
		const auto name = NextAutomaticName();
		if (name.empty()) {
			Notify("Tailor Capture: no free outfit name.");
			return;
		}
		if (const auto error = SaveOutfit(name, armors, {})) {
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
	// Three write_call<5> hooks need 14 bytes each; 64 leaves room to grow.
	SKSE::Init(skse, { .trampoline = true, .trampolineSize = 64 });
	SetupLog();
	logger::info("TailorOutfitCapture loaded");
	Settings::Load();
	Hooks::Install();
	return SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
}
