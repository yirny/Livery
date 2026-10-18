// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants      - slots that decide an outfit's armor class and gender
//   Library writes - SaveOutfit adds the outfit to Livery's library (Library); UpdateOutfit
//                    replaces a confirmed existing outfit's pieces and categories
//   Preview        - the captured actor wears the ticked pieces, or the outfit selected on the
//                    outfit pages, through OutfitPreview, seen through PreviewCamera, while the
//                    window's Live Preview tick is on; saving is off and an NPC is held where they
//                    stand (MovementHold) while the window is open, and every close of the window,
//                    a save included, ends them all
//   Capture        - hotkey handler: check the target (the player when nothing is aimed at), collect inventory
//                    armor, detect the armor class, list the library's outfits (adding their pieces the actor
//                    does not carry), then open the window with the worn pieces ticked (or save those under
//                    an automatic name where the overlay is unavailable)
//   Event sinks    - crosshair target, keys: Livery's assignment keys (Assignments) first, then the capture hotkey
//   Messaging      - outfit library, settings page, sink and menu registration, ready state
//   Entry          - log, settings, co-save, trampoline, hooks
#include "PCH.h"

#include "Assignments.h"
#include "CaptureWindow.h"
#include "Hooks.h"
#include "Library.h"
#include "MovementHold.h"
#include "OutfitPreview.h"
#include "PreviewCamera.h"
#include "Settings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	// Slots that decide an outfit's armor class and gender, most telling first:
	// Body sets how the outfit reads at a glance.
	using BipedSlot = RE::BGSBipedObjectForm::BipedObjectSlot;
	constexpr std::array kSlotPriority{ BipedSlot::kBody, BipedSlot::kHead, BipedSlot::kHands, BipedSlot::kFeet, BipedSlot::kCirclet };

	RE::ObjectRefHandle g_target{};
	bool                g_ready = false;

	void Notify(const std::string& text)
	{
		RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		logger::info("{}", text);
	}

	// --- Library writes -------------------------------------------------------

	// First "Captured Outfit NNN" the library does not have yet, or "" when it
	// has all of them.
	std::string NextAutomaticName()
	{
		for (int index = 1; index <= 99999; ++index) {
			auto name = std::format("Captured Outfit {:03}", index);
			if (!Library::FindByName(name)) {
				return name;
			}
		}
		return {};
	}

	std::vector<CaptureWindow::CategoryOption> AvailableCategories()
	{
		std::vector<CaptureWindow::CategoryOption> categories;
		for (const auto& [group, name] : Library::Categories()) {
			categories.push_back({ group, name });
		}
		return categories;
	}

	// Contract: game thread. Library::Create checks every refusal before it changes
	// anything, so a refusal never leaves an empty outfit. gender: -1 unisex, 0 male, 1 female.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> SaveOutfit(const std::string& name, const std::vector<RE::TESObjectARMO*>& armors,
		const std::vector<std::string>& categories, int gender)
	{
		if (auto error = Library::Create(name, gender, armors, categories)) {
			return error;
		}
		Notify(std::format("Livery: saved {} ({} pieces).", name, armors.size()));
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

	// Contract: game thread; the user confirmed replacing this outfit's pieces and
	// categories. Its pieces whose plugin is not loaded stay in it; the HUD message
	// says how many.
	// Returns an error message, or nullopt on success (HUD message shown).
	std::optional<std::string> UpdateOutfit(std::uint32_t id, const std::vector<RE::TESObjectARMO*>& armors, const std::vector<std::string>& categories)
	{
		const auto* outfit = Library::Find(id);
		if (!outfit) {
			return "The outfit no longer exists.";
		}
		const auto  name = outfit->name;  // the update leaves `outfit` stale
		std::size_t unloaded = 0;
		Library::LoadedPieces(*outfit, &unloaded);
		if (auto error = Library::Update(id, armors, categories)) {
			return error;
		}
		Notify(unloaded ? std::format("Livery: updated {} ({} pieces); {} piece(s) from plugins that are not loaded stay in it.", name, armors.size(), unloaded) :
						  std::format("Livery: updated {} ({} pieces).", name, armors.size()));
		return std::nullopt;
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

	// Inventory armor as plugin-defined forms, worn pieces first, each group in
	// name order. The library names a piece by its plugin, which runtime-created armor
	// (FormID 0xFF......) does not have, so player-enchanted gear is replaced by the plugin armor it was made from
	// (templateArmor, TNAM); runtime-created armor without one is left out. Only
	// worn pieces count in substituted and skipped. A form both worn and carried
	// is listed once, as worn.
	// Non-playable armor is left out uncounted: body-mod parts such as the BHUNP /
	// 3BA SMP objects (slot 48/50/51/60 per their MCM) are not clothing, and Dressing
	// leaves non-playable armor on.
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

	// Armor-type category of an outfit: the armor type of the first piece
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

	// Gender of an outfit (-1 unisex, 1 female): decided by the first
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

	// Index of the armor in `armors`; armor not there yet is appended to both
	// lists as a piece the actor does not carry, unticked.
	std::size_t PieceIndex(RE::TESObjectARMO* armor, std::vector<RE::TESObjectARMO*>& armors, std::vector<CaptureWindow::Piece>& pieces)
	{
		if (const auto found = std::ranges::find(armors, armor); found != armors.end()) {
			return static_cast<std::size_t>(found - armors.begin());
		}
		armors.push_back(armor);
		pieces.push_back({ .name = DisplayName(*armor), .selected = false, .slots = armor->GetSlotMask().underlying(), .owned = false });
		return armors.size() - 1;
	}

	// The library's outfits in case-insensitive name order, each with the offered
	// categories it is in and its loaded pieces as indices into `armors`, which
	// gains the pieces the actor does not carry (`pieces` matches it index for
	// index).
	// Contract: game thread (resolves the library's pieces to armor forms).
	std::vector<CaptureWindow::ExistingOutfit> ExistingOutfits(const std::vector<CaptureWindow::CategoryOption>& categories,
		std::vector<RE::TESObjectARMO*>& armors, std::vector<CaptureWindow::Piece>& pieces)
	{
		std::vector<const Library::Outfit*> library;
		for (const auto& outfit : Library::Outfits()) {
			library.push_back(&outfit);
		}
		std::ranges::sort(library, [](const Library::Outfit* a, const Library::Outfit* b) { return _stricmp(a->name.c_str(), b->name.c_str()) < 0; });

		std::vector<CaptureWindow::ExistingOutfit> outfits;
		outfits.reserve(library.size());
		for (const auto* stored : library) {
			CaptureWindow::ExistingOutfit outfit{ .name = stored->name, .id = stored->id };
			for (const auto& category : categories) {
				if (std::ranges::find(stored->categories, category.name) != stored->categories.end()) {
					outfit.categories.push_back(category.name);
				}
			}
			// LoadedPieces lists each form once, so the indices do not repeat.
			for (auto* armor : Library::LoadedPieces(*stored, &outfit.unloadedPieces)) {
				outfit.pieces.push_back(PieceIndex(armor, armors, pieces));
			}
			std::ranges::sort(outfit.pieces);
			outfits.push_back(std::move(outfit));
		}
		return outfits;
	}

	// Contract: game thread (library reads that hand out armor forms run here, so
	// the window itself never calls the library).
	// Every inventory piece is listed; worn ones start ticked and decide the
	// suggested armor class and gender. The library outfits' other pieces follow,
	// for Load Outfit.
	void OpenNameWindow(RE::Actor& actor, const std::vector<InventoryArmor>& inventory)
	{
		CaptureWindow::Request request;
		request.actorName = actor.GetName() ? actor.GetName() : "NPC";
		request.suggestedName = NextAutomaticName();
		request.categories = AvailableCategories();
		const auto  worn = WornArmors(inventory);
		const auto* armorClass = DetectArmorClass(worn);
		for (auto& category : request.categories) {
			category.selected = category.name == armorClass;
		}
		request.suggestedGender = DetectGender(worn);
		std::vector<RE::TESObjectARMO*> armors;
		armors.reserve(inventory.size());
		for (const auto& [armor, isWorn] : inventory) {
			request.pieces.push_back({ DisplayName(*armor), isWorn && !LooksLikeWig(*armor), armor->GetSlotMask().underlying() });
			armors.push_back(armor);
		}
		request.existingOutfits = ExistingOutfits(request.categories, armors, request.pieces);
		AttachPreview(request, actor, armors);
		// Every close, whatever closed the window, ends the preview, lets saving back on
		// and lets an NPC walk again.
		request.onClose = [] {
			PreviewCamera::End();
			OutfitPreview::End();
			AllowSaving();
			MovementHold::End();
		};
		request.onSave = [armors](const std::string& name, const std::vector<std::size_t>& pieces, const std::vector<std::string>& categories,
							 int gender) {
			if (const auto error = SaveOutfit(name, PickPieces(armors, pieces), categories, gender)) {
				CaptureWindow::SetError(*error);
			} else {
				CaptureWindow::Close();
			}
		};
		request.onUpdate = [armors = std::move(armors)](std::uint32_t id, const std::vector<std::size_t>& pieces, const std::vector<std::string>& categories) {
			if (const auto error = UpdateOutfit(id, PickPieces(armors, pieces), categories)) {
				CaptureWindow::SetError(*error);
			} else {
				CaptureWindow::Close();
			}
		};
		BlockSaving();
		MovementHold::Begin(actor);
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
			Notify("Livery: children's outfits cannot be captured.");
			return;
		}
		// The follower restriction narrows which NPCs qualify; the player always does.
		if (!isPlayer && Settings::FollowersAndSpouseOnly() && !IsFollowerOrSpouse(*actor)) {
			Notify("Livery: only followers and your spouse can be captured.");
			return;
		}

		std::size_t skipped = 0;
		std::size_t substituted = 0;
		const auto  inventory = CollectArmor(*actor, skipped, substituted);
		const auto  armors = WornArmors(inventory);
		// The window also offers carried armor and Load Outfit, so it opens even when
		// the actor carries no armor; only the automatic save below needs worn armor.
		const bool hasWindow = Hooks::IsRenderReady();
		if (!hasWindow && armors.empty()) {
			Notify(skipped ? "Livery: worn armor was created in-game and cannot be saved." : "Livery: no worn armor was found.");
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
			Notify("Livery: no free outfit name.");
			return;
		}
		const std::vector<std::string> categories{ DetectArmorClass(armors) };
		if (const auto error = SaveOutfit(name, armors, categories, DetectGender(armors))) {
			Notify("Livery: " + *error);
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
				if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) {
					continue;
				}
				// Livery's keys first: F8 releases everyone even while it is the capture hotkey too.
				if (Assignments::HandleKey(button->GetIDCode(), g_target)) {
					break;
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
			Settings::RegisterMenu();
			break;

		case SKSE::MessagingInterface::kInputLoaded:
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&InputSink::Get());
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			Library::Load();  // pieces resolve against the loaded plugins, so not before this
			SKSE::GetCrosshairRefEventSource()->AddEventSink(&CrosshairSink::Get());
			Assignments::RegisterSinks();
			Hooks::RegisterMenu();
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
			g_ready = false;
			g_target = {};
			// The world the preview was set in is being replaced, inventories included: drop
			// the preview without dressing anyone.
			OutfitPreview::Abandon();
			CaptureWindow::Close();  // discard a pending capture; its actor is going away
			break;

		case SKSE::MessagingInterface::kNewGame:
			OutfitPreview::Abandon();  // a new game replaces the world without a kPreLoadGame
			[[fallthrough]];
		case SKSE::MessagingInterface::kPostLoadGame:
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
	// Two write_call<5> hooks need 14 bytes each; 64 leaves room to grow.
	SKSE::Init(skse, { .trampoline = true, .trampolineSize = 64 });
	SetupLog();
	logger::info("Livery loaded");
	Settings::Load();
	Assignments::RegisterCoSave();
	Hooks::Install();
	return SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
}
