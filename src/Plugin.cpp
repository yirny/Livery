// SPDX-License-Identifier: GPL-3.0-or-later
#include "PCH.h"
#include "TailorAPI.h"

#include <cstdint>
#include <format>
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

	void Capture(RE::ObjectRefHandle handle)
	{
		if (!g_ready || !g_tailor) {
			Notify("Tailor Capture: Tailor 3.0+ is not ready.");
			return;
		}

		const auto ref = handle.get();
		auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor || actor == RE::PlayerCharacter::GetSingleton()) {
			Notify("Tailor Capture: aim at an NPC.");
			return;
		}

		// Read the currently worn armor records, not the NPC's default outfit.
		const auto inventory = actor->GetInventory([](RE::TESBoundObject& item) {
			return item.IsArmor();
		});

		std::vector<RE::TESObjectARMO*> armors;
		armors.reserve(inventory.size());
		std::size_t runtimeCreated = 0;

		for (const auto& [object, itemData] : inventory) {
			const auto& [count, entry] = itemData;
			if (count <= 0 || !entry || !entry->IsWorn()) {
				continue;
			}
			auto* armor = object->As<RE::TESObjectARMO>();
			if (!armor) {
				continue;
			}
			// AddArmorToOutfit always refuses armor the game created at runtime
			// (FormID 0xFF......, e.g. player-enchanted gear). Skip it before
			// creating the outfit, because API 1.0 cannot delete an empty outfit.
			if (armor->IsDynamicForm()) {
				++runtimeCreated;
				logger::info("Skipped runtime-created armor {:08X}", armor->GetFormID());
				continue;
			}
			armors.push_back(armor);
		}

		if (armors.empty()) {
			Notify(runtimeCreated ?
					   "Tailor Capture: worn armor was created in-game and cannot be saved." :
					   "Tailor Capture: no worn armor was found.");
			return;
		}

		if (!g_tailor->DoesCategoryExist(kCategory) &&
			!g_tailor->CreateCustomCategory(kCategory)) {
			Notify("Tailor Capture: could not create the category.");
			return;
		}

		// Tailor outfit names must be unique across all saves.
		std::string name;
		for (int index = 1; index <= 99999; ++index) {
			name = std::format("Captured Outfit {:03}", index);
			if (!g_tailor->DoesOutfitExist(name.c_str())) {
				break;
			}
			name.clear();
		}

		if (name.empty() || !g_tailor->CreateOutfit(name.c_str(), "", -1)) {
			Notify("Tailor Capture: could not create an outfit.");
			return;
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
			// Tailor API 1.0 has no DeleteOutfit operation.
			Notify("Tailor Capture: unsupported armor; delete the empty outfit in Tailor.");
			return;
		}

		if (!g_tailor->AddCategory(name.c_str(), kCategory)) {
			Notify("Tailor Capture: saved, but category assignment failed.");
			return;
		}

		Notify(std::format("Tailor: saved {} ({} pieces).", name, added));
		if (added != armors.size() || runtimeCreated) {
			logger::warn("Saved {} of {} worn pieces ({} runtime-created skipped)",
				added, armors.size() + runtimeCreated, runtimeCreated);
		}
	}

	class CrosshairSink final : public RE::BSTEventSink<SKSE::CrosshairRefEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(
			const SKSE::CrosshairRefEvent* event,
			RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override
		{
			g_target = event && event->crosshairRef ?
			               event->crosshairRef->GetHandle() :
			               RE::ObjectRefHandle{};
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
		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const* events,
			RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!events || !*events || !g_ready ||
				RE::UI::GetSingleton()->GameIsPaused()) {
				return RE::BSEventNotifyControl::kContinue;
			}

			for (auto* event = *events; event; event = event->next) {
				auto* button = event->AsButtonEvent();
				if (button &&
					button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
					button->GetIDCode() == kHotkey && button->IsDown()) {
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
	SKSE::Init(skse);
	SetupLog();
	logger::info("TailorOutfitCapture loaded");
	return SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
}
