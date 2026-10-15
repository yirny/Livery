// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants      - hotkey, category name
//   Tailor writes  - SaveOutfit creates the outfit, adds pieces and the category
//   Capture        - F8 handler: collect worn armor, then open the name window
//                    (or save under an automatic name where the overlay is unavailable)
//   Event sinks    - crosshair target, hotkey
//   Messaging      - Tailor interface, sink registration, ready state
//   Entry          - log, trampoline, hooks
#include "PCH.h"

#include "CaptureWindow.h"
#include "Hooks.h"
#include "TailorAPI.h"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace
{
	constexpr std::uint32_t kHotkey = 0x42;  // F8 (DirectInput scan code)
	constexpr const char*   kCategory = "Captured";

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

	// Contract: game thread; Tailor 3.0 library already loaded (g_ready).
	// API 1.0 cannot delete an outfit, so every refusal that can be checked up
	// front is checked before CreateOutfit. Returns an error message, or
	// nullopt on success (then the HUD message has been shown).
	std::optional<std::string> SaveOutfit(const std::string& name, const std::vector<RE::TESObjectARMO*>& armors)
	{
		if (!g_ready || !g_tailor) {
			return "Tailor 3.0+ is not ready.";
		}
		if (g_tailor->DoesOutfitExist(name.c_str())) {
			return std::format("An outfit named \"{}\" already exists.", name);
		}
		if (!g_tailor->DoesCategoryExist(kCategory) && !g_tailor->CreateCustomCategory(kCategory)) {
			return "Could not create the Captured category.";
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
		if (!g_tailor->AddCategory(name.c_str(), kCategory)) {
			Notify(std::format("Tailor: saved {} ({} pieces), but category assignment failed.", name, added));
			return std::nullopt;
		}

		Notify(std::format("Tailor: saved {} ({} pieces).", name, added));
		if (added != armors.size()) {
			logger::warn("Saved {} of {} worn pieces", added, armors.size());
		}
		return std::nullopt;
	}

	// --- Capture --------------------------------------------------------------

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

	void OpenNameWindow(const RE::Actor& actor, std::vector<RE::TESObjectARMO*> armors)
	{
		CaptureWindow::Request request;
		request.actorName = actor.GetName() ? actor.GetName() : "NPC";
		request.suggestedName = NextAutomaticName();
		for (const auto* armor : armors) {
			request.pieceNames.push_back(DisplayName(*armor));
		}
		request.onSave = [armors = std::move(armors)](const std::string& name) {
			if (const auto error = SaveOutfit(name, armors)) {
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

		// No overlay (Skyrim VR, or ImGui failed to initialise): automatic name.
		const auto name = NextAutomaticName();
		if (name.empty()) {
			Notify("Tailor Capture: no free outfit name.");
			return;
		}
		if (const auto error = SaveOutfit(name, armors)) {
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
	Hooks::Install();
	return SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
}
