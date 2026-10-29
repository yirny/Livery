// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keep imgui.h out of this file: SKSEMenuFramework.h defines IM_COL32 and other
// ImGui macros of its own. Its ImGuiMCP calls draw with SKSE Menu Framework's
// ImGui context, not the one Hooks.cpp creates.
#include "PCH.h"

#include "Settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

#include <SKSEMenuFramework.h>

#include "sarrai/Palettes.h"

namespace
{
	constexpr auto kIniSection = L"General";
	constexpr auto kRestrictKey = L"RestrictToFollowersAndSpouse";
	constexpr auto kLivePreviewKey = L"LivePreview";
	constexpr auto kNpcReactionKey = L"NpcReaction";
	constexpr auto kUiSection = L"UI";
	constexpr auto kPaletteKey = L"Palette";
	constexpr auto kMenuSoundsKey = L"MenuSounds";
	constexpr auto kHotkeySection = L"Hotkey";
	constexpr auto kHotkeyKeyKey = L"Key";
	constexpr auto kHotkeyModifierKey = L"Modifier";
	constexpr auto kCameraSection = L"Camera";
	constexpr auto kSpeedPercentKey = L"SpeedPercent";
	constexpr auto kPlayerSection = L"Player";
	constexpr auto kAddMissingKey = L"AddMissingPieces";
	constexpr auto kWindowSection = L"Window";
	constexpr auto kShowPlayerInventoryKey = L"ShowPlayerInventory";
	constexpr auto kShowPlayerWornKey = L"ShowPlayerWornPieces";
	constexpr auto kShowSlotColumnKey = L"ShowSlotColumn";
	constexpr auto kTransferSection = L"Transfer";
	constexpr auto kMoveFromPlayerKey = L"MoveFromPlayer";
	constexpr auto kProtectFavoritesKey = L"ProtectFavorites";
	constexpr auto kSituationsSection = L"Situations";
	constexpr auto kNotifyChangesKey = L"NotifyChanges";
	constexpr auto kUninstallSection = L"Uninstall";
	constexpr auto kReleaseEveryoneKey = L"ReleaseEveryone";

	constexpr auto kPaletteDir = L"Data/SKSE/Plugins/Livery/palettes";
	constexpr auto kDefaultPalette = L"Modern";  // keep in step with kIniKeys

	constexpr auto kMenuSource = "SKSE Menu Framework";  // the settings page, as the log names it

	constexpr float kDefaultSpeedPercent = 100.0f;  // keep in step with kIniKeys
	constexpr float kMinSpeedPercent = 10.0f;
	constexpr float kMaxSpeedPercent = 200.0f;

	using Key = RE::BSKeyboardDevice::Keys::Key;
	constexpr std::uint32_t kDefaultHotkey = Key::kX;            // keep in step with kIniKeys
	constexpr std::uint32_t kDefaultModifier = Key::kLeftShift;  // keep in step with kIniKeys
	constexpr std::uint32_t kNoModifier = 0;

	struct IniKey
	{
		const wchar_t* section;
		const wchar_t* key;
		const wchar_t* defaultValue;
		const wchar_t* comment;  // written as "; " lines above the key; '\n' separates lines
	};

	// Every key the INI can hold, grouped by section. The INI is not shipped: it is
	// created from this table on first start, and keys a newer version adds are
	// appended to the player's file, so an update never resets their settings.
	constexpr std::array<IniKey, 16> kIniKeys{ {
		{ kIniSection, kRestrictKey, L"1",
			L"1 = the hotkey works only on your followers and spouse. 0 = it works on any adult NPC.\n"
			L"You can always open Livery for yourself." },
		{ kIniSection, kLivePreviewKey, L"1",
			L"1 = Livery opens with Live Preview on: the game keeps running and the character wears the ticked items.\n"
			L"0 = it opens with Live Preview off and the game paused." },
		{ kIniSection, kNpcReactionKey, L"1",
			L"1 = with Live Preview on, the NPC turns toward you and greets you when Livery opens." },
		{ kUiSection, kPaletteKey, L"Modern",
			L"Window skin: a palette file in Data/SKSE/Plugins/Livery/palettes, named without .ini\n"
			L"(Modern, Edge, Vanilla, Nordic, or your own copy of one)." },
		{ kUiSection, kMenuSoundsKey, L"1",
			L"1 = Livery plays the game's menu sounds as it opens and as you click in it." },
		{ kHotkeySection, kHotkeyKeyKey, L"45",
			L"Opens Livery for the NPC under your crosshair, or for yourself if no NPC is targeted.\n"
			L"A DirectInput scan code (45 = X)." },
		{ kHotkeySection, kHotkeyModifierKey, L"42",
			L"Key to hold with the hotkey: 0 = none, 42 = Shift, 29 = Ctrl, 56 = Alt. Left and right count the same." },
		{ kCameraSection, kSpeedPercentKey, L"100",
			L"Preview camera speed in percent (10 to 200, 100 = normal): W / A / S / D, mouse drag and wheel zoom.\n"
			L"Hold Shift to move at half speed." },
		{ kPlayerSection, kAddMissingKey, L"0",
			L"1 = dressing yourself creates the items you don't carry in your inventory, for free.\n"
			L"0 = it uses only the items you carry, and a message names the rest.\n"
			L"In game this is Dress yourself from inventory only, reversed." },
		{ kWindowSection, kShowPlayerInventoryKey, L"1",
			L"1 = an NPC's window also lists your armor, under \"<your name>'s Inventory\"." },
		{ kWindowSection, kShowPlayerWornKey, L"0",
			L"1 = that list includes your equipped armor. 0 = it leaves it out unless you carry another copy.\n"
			L"In game this is Exclude equipped items, reversed." },
		{ kWindowSection, kShowSlotColumnKey, L"0",
			L"1 = the item list shows a Slot column: each item's body slot numbers (32, 34)." },
		{ kTransferSection, kMoveFromPlayerKey, L"1",
			L"1 = Apply on an NPC transfers the ticked items from your inventory, enchantments and tempering included.\n"
			L"0 = the NPC gets plain copies and you keep yours." },
		{ kTransferSection, kProtectFavoritesKey, L"1",
			L"1 = your inventory list in an NPC's window leaves out your favorites. Quest items are always left out." },
		{ kSituationsSection, kNotifyChangesKey, L"0",
			L"1 = a message tells you when a character changes outfit as a situation (Town, Cold, ...) starts or ends." },
		{ kUninstallSection, kReleaseEveryoneKey, L"0",
			L"1 = each time you load a save, Livery takes its outfits off everyone and tells you, until you set this back to 0.\n"
			L"Use it before you uninstall Livery if you don't have SKSE Menu Framework, whose Livery page has a Release everyone button." },
	} };

	// A modifier is named by its left-hand code in the INI and the picker; either side counts.
	struct Modifier
	{
		std::uint32_t left;
		std::uint32_t right;
	};
	constexpr std::array<Modifier, 3> kModifiers{ {
		{ Key::kLeftShift, Key::kRightShift },
		{ Key::kLeftControl, Key::kRightControl },
		{ Key::kLeftAlt, Key::kRightAlt },
	} };

	std::atomic<bool>          g_followersAndSpouseOnly{ true };
	std::atomic<bool>          g_livePreview{ true };
	std::atomic<bool>          g_npcReaction{ true };
	std::atomic<bool>          g_menuSounds{ true };
	std::atomic<std::uint32_t> g_hotkey{ kDefaultHotkey };
	std::atomic<std::uint32_t> g_modifier{ kDefaultModifier };  // left-hand code of a kModifiers entry, or kNoModifier
	std::atomic<bool>          g_pickingHotkey{ false };        // the settings page waits for the next key
	std::atomic<float>         g_speedPercent{ kDefaultSpeedPercent };
	std::atomic<bool>          g_playerAddsMissing{ false };
	std::atomic<bool>          g_showPlayerInventory{ true };
	std::atomic<bool>          g_showPlayerWorn{ false };
	std::atomic<bool>          g_showSlotColumn{ false };
	std::atomic<bool>          g_protectFavorites{ true };
	std::atomic<bool>          g_moveFromPlayer{ true };
	std::atomic<bool>          g_notifySituationChanges{ false };
	std::atomic<bool>          g_releaseEveryoneOnLoad{ false };
	std::filesystem::path      g_iniPath;   // set once by Load, read-only afterwards
	void (*g_releaseEveryone)() = nullptr;  // set once by RegisterMenu, before the page can render

	// --- Keys -----------------------------------------------------------------

	// Accepts the left or right code of a modifier; nullptr for any other key.
	const Modifier* FindModifier(std::uint32_t a_code)
	{
		for (const auto& modifier : kModifiers) {
			if (a_code == modifier.left || a_code == modifier.right) {
				return &modifier;
			}
		}
		return nullptr;
	}

	// Contract: reads the keyboard's current DirectInput state, so it is only meaningful
	// while a key event is being handled on the input path. Read directly: linking
	// BSWin32KeyboardDevice::IsPressed pulls in virtuals CommonLibSSE-NG does not define.
	bool IsModifierHeld(const Modifier& a_modifier)
	{
		auto* keyboard = RE::BSInputDeviceManager::GetSingleton()->GetKeyboard();
		if (!keyboard) {
			return false;
		}
		const auto& state = keyboard->GetRuntimeData().curState;
		const auto  pressed = [&](std::uint32_t code) { return code < std::size(state) && (state[code] & 0x80) != 0; };
		return pressed(a_modifier.left) || pressed(a_modifier.right);
	}

	// Windows names the key from its scan code; DirectInput marks extended keys with 0x80.
	std::string KeyName(std::uint32_t a_scanCode)
	{
		constexpr std::uint32_t kExtendedBit = 0x80;
		LONG                    lParam = static_cast<LONG>((a_scanCode & ~kExtendedBit) << 16);
		if (a_scanCode & kExtendedBit) {
			lParam |= 1 << 24;
		}
		wchar_t name[64]{};
		if (::GetKeyNameTextW(lParam, name, static_cast<int>(std::size(name))) > 0) {
			return SKSE::stl::utf16_to_utf8(name).value_or(std::format("Key {}", a_scanCode));
		}
		return std::format("Key {}", a_scanCode);
	}

	std::string HotkeyName()
	{
		const auto modifier = g_modifier.load();
		const auto key = KeyName(g_hotkey.load());
		return modifier == kNoModifier ? key : KeyName(modifier) + " + " + key;
	}

	// --- INI ------------------------------------------------------------------

	// Writes a fresh INI holding every key with its comment and default.
	void WriteIniTemplate()
	{
		std::wofstream file{ g_iniPath };
		if (!file) {
			logger::warn("Could not create Livery.ini");
			return;
		}
		const wchar_t* section = nullptr;
		for (const auto& entry : kIniKeys) {
			if (!section || std::wcscmp(section, entry.section) != 0) {
				if (section) {
					file << L'\n';
				}
				file << L'[' << entry.section << L"]\n";
				section = entry.section;
			}
			for (std::wstring_view comment = entry.comment; !comment.empty();) {
				const auto end = comment.find(L'\n');
				file << L"; " << comment.substr(0, end) << L'\n';
				comment = end == std::wstring_view::npos ? std::wstring_view{} : comment.substr(end + 1);
			}
			file << entry.key << L'=' << entry.defaultValue << L'\n';
		}
		logger::info("Created Livery.ini with default settings");
	}

	// Appends keys a newer version introduced to an INI the player already has.
	// The profile API cannot write comments, so these keys get none.
	void AddMissingIniKeys()
	{
		constexpr auto kMissing = L"\x01";  // never a real value
		wchar_t        value[8]{};
		for (const auto& entry : kIniKeys) {
			::GetPrivateProfileStringW(entry.section, entry.key, kMissing, value, static_cast<DWORD>(std::size(value)),
				g_iniPath.c_str());
			if (std::wstring_view{ value } != kMissing) {
				continue;
			}
			if (!::WritePrivateProfileStringW(entry.section, entry.key, entry.defaultValue, g_iniPath.c_str())) {
				logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
				return;
			}
			logger::info("Added missing INI key [{}] {}", SKSE::stl::utf16_to_utf8(entry.section).value_or(""),
				SKSE::stl::utf16_to_utf8(entry.key).value_or(""));
		}
	}

	void SaveFollowersAndSpouseOnly(bool value)
	{
		if (!::WritePrivateProfileStringW(kIniSection, kRestrictKey, value ? L"1" : L"0", g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	void SaveLivePreview(bool value)
	{
		if (!::WritePrivateProfileStringW(kIniSection, kLivePreviewKey, value ? L"1" : L"0", g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	void SaveMenuSounds(bool value)
	{
		if (!::WritePrivateProfileStringW(kUiSection, kMenuSoundsKey, value ? L"1" : L"0", g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	void SavePlayerAddsMissing(bool value)
	{
		if (!::WritePrivateProfileStringW(kPlayerSection, kAddMissingKey, value ? L"1" : L"0", g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	void SaveFlag(const wchar_t* a_section, const wchar_t* a_key, bool a_value)
	{
		if (!::WritePrivateProfileStringW(a_section, a_key, a_value ? L"1" : L"0", g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	void SaveSpeedPercent(float a_percent)
	{
		const auto value = std::format(L"{:.0f}", a_percent);
		if (!::WritePrivateProfileStringW(kCameraSection, kSpeedPercentKey, value.c_str(), g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	void SavePalette(const std::wstring& a_stem)
	{
		if (!::WritePrivateProfileStringW(kUiSection, kPaletteKey, a_stem.c_str(), g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	void SaveHotkey(std::uint32_t a_key, std::uint32_t a_modifier)
	{
		const auto key = std::to_wstring(a_key);
		const auto modifier = std::to_wstring(a_modifier);
		if (!::WritePrivateProfileStringW(kHotkeySection, kHotkeyKeyKey, key.c_str(), g_iniPath.c_str()) ||
			!::WritePrivateProfileStringW(kHotkeySection, kHotkeyModifierKey, modifier.c_str(), g_iniPath.c_str())) {
			logger::warn("Could not write Livery.ini (error {})", ::GetLastError());
		}
	}

	// --- SKSE Menu Framework page ---------------------------------------------

	// Contract: SKSE Menu Framework calls this on the game's input path for every event,
	// its menu open or not; returning true keeps the event from the game. Idle unless the
	// settings page is waiting for a key. A modifier pressed alone keeps waiting, so the
	// player can hold it and then press the key it modifies; Esc cancels.
	bool __stdcall OnInput(RE::InputEvent* a_event)
	{
		if (!g_pickingHotkey.load()) {
			return false;
		}
		auto* button = a_event ? a_event->AsButtonEvent() : nullptr;
		if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) {
			return false;
		}

		const auto code = button->GetIDCode();
		if (code == Key::kEscape) {
			g_pickingHotkey = false;
			return true;
		}
		if (FindModifier(code)) {
			return true;
		}

		auto modifier = kNoModifier;
		for (const auto& candidate : kModifiers) {
			if (IsModifierHeld(candidate)) {
				modifier = candidate.left;
				break;
			}
		}
		g_hotkey = code;
		g_modifier = modifier;
		g_pickingHotkey = false;
		SaveHotkey(code, modifier);
		logger::info("Hotkey = {} ({}, modifier {}) (SKSE Menu Framework)", HotkeyName(), code, modifier);
		return true;
	}

	// Puts every setting on the page but the palette (the player's choice of look, and the
	// camera speed is reset with the rest) back to its default, in memory and in the INI.
	// Contract: render thread (RenderSettings), after the player confirmed. Keep in step
	// with Load and kIniKeys.
	void RestoreDefaultSettings()
	{
		g_pickingHotkey = false;
		g_hotkey = kDefaultHotkey;
		g_modifier = kDefaultModifier;
		SaveHotkey(kDefaultHotkey, kDefaultModifier);
		g_notifySituationChanges = false;
		SaveFlag(kSituationsSection, kNotifyChangesKey, false);
		g_followersAndSpouseOnly = true;
		SaveFollowersAndSpouseOnly(true);
		g_npcReaction = true;
		SaveFlag(kIniSection, kNpcReactionKey, true);
		g_moveFromPlayer = true;
		SaveFlag(kTransferSection, kMoveFromPlayerKey, true);
		g_playerAddsMissing = false;
		SavePlayerAddsMissing(false);
		g_livePreview = true;
		SaveLivePreview(true);
		g_speedPercent = kDefaultSpeedPercent;
		SaveSpeedPercent(kDefaultSpeedPercent);
		g_showPlayerInventory = true;
		SaveFlag(kWindowSection, kShowPlayerInventoryKey, true);
		g_showPlayerWorn = false;
		SaveFlag(kWindowSection, kShowPlayerWornKey, false);
		g_protectFavorites = true;
		SaveFlag(kTransferSection, kProtectFavoritesKey, true);
		g_showSlotColumn = false;
		SaveFlag(kWindowSection, kShowSlotColumnKey, false);
		g_menuSounds = true;
		SaveMenuSounds(true);
		logger::info("Settings restored to their defaults (SKSE Menu Framework)");
	}

	// Restore default settings, asked once more in a popup.
	// Contract: render thread (RenderSettings).
	void RenderRestoreDefaults()
	{
		constexpr auto kPopup = "Restore default settings?###RestoreDefaults";
		if (ImGuiMCP::Button("Restore default settings")) {
			ImGuiMCP::OpenPopup(kPopup);
		}
		ImGuiMCP::SetItemTooltip("Puts every setting on this page back to its default. Window skin keeps your choice.");
		if (!ImGuiMCP::BeginPopupModal(kPopup, nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		ImGuiMCP::Text("Restore every setting on this page to its default? Window skin stays as it is.");
		if (ImGuiMCP::Button("Yes")) {
			RestoreDefaultSettings();
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("No")) {
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::EndPopup();
	}

	// The uninstall step, asked once more in a popup since it undoes every assignment.
	// Contract: render thread (RenderSettings); g_releaseEveryone only queues an SKSE task,
	// which checks whether the release can run now.
	void RenderReleaseEveryone()
	{
		constexpr auto kPopup = "Release everyone?###ReleaseEveryone";
		if (ImGuiMCP::Button("Release everyone")) {
			ImGuiMCP::OpenPopup(kPopup);
		}
		ImGuiMCP::SetItemTooltip(
			"Takes Livery's outfits off everyone, player included, and forgets their situational outfits. "
			"Do this before you uninstall Livery.");
		if (!ImGuiMCP::BeginPopupModal(kPopup, nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		ImGuiMCP::Text("Release every character Livery dresses? Do this before you uninstall Livery.");
		if (ImGuiMCP::Button("Yes")) {
			if (g_releaseEveryone) {
				g_releaseEveryone();
			}
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("No")) {
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::EndPopup();
	}

	// The palette dropdown, one row per palette file by its label, and Restore original palette
	// beside it, shown only while the chosen palette is one Livery ships with its skin or a
	// colour edited. Both take effect the next time the capture window opens.
	// Contract: render thread (RenderSettings).
	void RenderWindowSkin()
	{
		constexpr float kComboWidthEms = 14.0f;  // the dropdown's width, of the font size
		constexpr auto  kPopup = "Restore original palette?###RestorePalette";

		const auto palettes = Settings::ListPalettes();
		if (palettes.labels.empty()) {
			return;
		}
		ImGuiMCP::AlignTextToFramePadding();
		ImGuiMCP::Text("Window skin");
		ImGuiMCP::SameLine();
		ImGuiMCP::SetNextItemWidth(ImGuiMCP::GetFontSize() * kComboWidthEms);
		if (ImGuiMCP::BeginCombo("##WindowSkin", palettes.labels[palettes.chosen].c_str())) {
			for (std::size_t i = 0; i < palettes.labels.size(); ++i) {
				ImGuiMCP::PushID(static_cast<int>(i));
				if (ImGuiMCP::Selectable(palettes.labels[i].c_str(), i == palettes.chosen)) {
					Settings::ChoosePalette(i, kMenuSource);
				}
				ImGuiMCP::PopID();
			}
			ImGuiMCP::EndCombo();
		}
		ImGuiMCP::SetItemTooltip("Takes effect the next time you open the window.");

		if (palettes.edited) {
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Restore original palette")) {
				ImGuiMCP::OpenPopup(kPopup);
			}
			ImGuiMCP::SetItemTooltip("Writes the palette file back as Livery ships it.");
		}
		if (!ImGuiMCP::BeginPopupModal(kPopup, nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		ImGuiMCP::Text("Restore %s.ini as Livery ships it? Your changes to it will be lost.", palettes.file.c_str());
		if (ImGuiMCP::Button("Yes")) {
			Settings::RestoreChosenPalette(kMenuSource);
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("No")) {
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::EndPopup();
	}

	// The gap between two settings under one heading; it scales with the framework's font.
	void SettingGap()
	{
		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();
	}

	// A heading with a rule, with room above it so the sections read apart.
	void Heading(const char* a_label)
	{
		constexpr float kRoomAboveInFrames = 0.75f;  // of the frame height, over the setting gap
		SettingGap();
		ImGuiMCP::Dummy(ImGuiMCP::ImVec2{ 0.0f, ImGuiMCP::GetFrameHeight() * kRoomAboveInFrames });
		ImGuiMCP::SeparatorText(a_label);
	}

	// Contract: SKSE Menu Framework calls this on its render thread every frame
	// the page is shown. The INI is written only when the player clicks.
	// Labels are short; each setting explains itself in a tooltip (CLAUDE.md, player-facing English).
	void __stdcall RenderSettings()
	{
		ImGuiMCP::SeparatorText("General");

		// The next key press is taken by OnInput; "###" keeps the button's ID while its text changes.
		ImGuiMCP::AlignTextToFramePadding();
		ImGuiMCP::Text("Hotkey");
		ImGuiMCP::SameLine();
		const bool picking = g_pickingHotkey.load();
		const auto label = picking ? std::string{ "Press a key (Esc cancels)###Hotkey" } : HotkeyName() + "###Hotkey";
		if (ImGuiMCP::Button(label.c_str())) {
			g_pickingHotkey = !picking;
		}
		ImGuiMCP::SetItemTooltip("Opens Livery for the NPC under your crosshair, or for yourself if no NPC is targeted.\nDefault: Shift + X");

		SettingGap();

		// Applies at once.
		bool notifyChanges = g_notifySituationChanges.load();
		if (ImGuiMCP::Checkbox("Outfit change notifications", &notifyChanges)) {
			g_notifySituationChanges = notifyChanges;
			SaveFlag(kSituationsSection, kNotifyChangesKey, notifyChanges);
			logger::info("NotifyChanges = {} (SKSE Menu Framework)", notifyChanges);
		}
		ImGuiMCP::SetItemTooltip("Notifies you when a character changes outfit as a situation starts or ends.\nDefault: OFF");

		// Every tick here is immersive when on.
		Heading("Immersion");

		bool followersOnly = g_followersAndSpouseOnly.load();
		if (ImGuiMCP::Checkbox("Followers and spouse only", &followersOnly)) {
			g_followersAndSpouseOnly = followersOnly;
			SaveFollowersAndSpouseOnly(followersOnly);
			logger::info("RestrictToFollowersAndSpouse = {} (SKSE Menu Framework)", followersOnly);
		}
		ImGuiMCP::SetItemTooltip("Restricts the hotkey to your followers and spouse. You can always open Livery for yourself.\nDefault: ON");

		SettingGap();

		// Takes effect the next time the window opens.
		bool npcReaction = g_npcReaction.load();
		if (ImGuiMCP::Checkbox("NPC reaction", &npcReaction)) {
			g_npcReaction = npcReaction;
			SaveFlag(kIniSection, kNpcReactionKey, npcReaction);
			logger::info("NpcReaction = {} (SKSE Menu Framework)", npcReaction);
		}
		ImGuiMCP::SetItemTooltip("With Live Preview on, the NPC turns toward you and greets you when the window opens.\nDefault: ON");

		SettingGap();

		// Takes effect at the next Apply.
		bool moveFromPlayer = g_moveFromPlayer.load();
		if (ImGuiMCP::Checkbox("Transfer items to NPCs", &moveFromPlayer)) {
			g_moveFromPlayer = moveFromPlayer;
			SaveFlag(kTransferSection, kMoveFromPlayerKey, moveFromPlayer);
			logger::info("MoveFromPlayer = {} (SKSE Menu Framework)", moveFromPlayer);
		}
		ImGuiMCP::SetItemTooltip(
			"On Apply, transfers items from your inventory to the NPC, preserving enchantments and tempering.\n"
			"Off: the NPC receives plain copies, and you keep yours.\n"
			"Default: ON");

		SettingGap();

		// Takes effect the next time you dress yourself. The tick is the opposite of AddMissingPieces,
		// so that on means immersive like the rest of the section; the INI key keeps its meaning.
		bool inventoryOnly = !g_playerAddsMissing.load();
		if (ImGuiMCP::Checkbox("Dress yourself from inventory only", &inventoryOnly)) {
			g_playerAddsMissing = !inventoryOnly;
			SavePlayerAddsMissing(!inventoryOnly);
			logger::info("AddMissingPieces = {} (SKSE Menu Framework)", !inventoryOnly);
		}
		ImGuiMCP::SetItemTooltip(
			"Dressing yourself uses only items you carry. NPCs always get what they need, but you can't take those items from them.\n"
			"Off: missing items are created in your inventory for free, which can break game balance. Keeping this on is strongly recommended.\n"
			"Default: ON");

		Heading("Window");

		// Takes effect the next time the window opens.
		bool livePreview = g_livePreview.load();
		if (ImGuiMCP::Checkbox("Live Preview on open", &livePreview)) {
			g_livePreview = livePreview;
			SaveLivePreview(livePreview);
			logger::info("LivePreview = {} (SKSE Menu Framework)", livePreview);
		}
		ImGuiMCP::SetItemTooltip("Starts with Live Preview enabled each time you open Livery.\nDefault: ON");

		SettingGap();

		// Applies at once; the INI is written when the slider is let go.
		float percent = g_speedPercent.load();
		if (ImGuiMCP::SliderFloat("Preview camera speed", &percent, kMinSpeedPercent, kMaxSpeedPercent, "%.0f%%")) {
			g_speedPercent = std::clamp(percent, kMinSpeedPercent, kMaxSpeedPercent);
		}
		if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
			SaveSpeedPercent(g_speedPercent.load());
			logger::info("SpeedPercent = {:.0f} (SKSE Menu Framework)", g_speedPercent.load());
		}
		ImGuiMCP::SetItemTooltip("Adjusts camera movement speed. Use WASD, mouse drag, and scroll wheel to control the camera.");

		SettingGap();

		// The three below take effect the next time the window opens. The two under the first
		// only matter while it is on, so they are greyed otherwise.
		bool showInventory = g_showPlayerInventory.load();
		if (ImGuiMCP::Checkbox("List your inventory for NPCs", &showInventory)) {
			g_showPlayerInventory = showInventory;
			SaveFlag(kWindowSection, kShowPlayerInventoryKey, showInventory);
			logger::info("ShowPlayerInventory = {} (SKSE Menu Framework)", showInventory);
		}
		ImGuiMCP::SetItemTooltip("In an NPC's window, also lists your armor under \"<your name>'s Inventory\".\nDefault: ON");
		ImGuiMCP::Indent();
		ImGuiMCP::BeginDisabled(!showInventory);
		// The tick is the opposite of ShowPlayerWornPieces, so that on excludes like the tick
		// below it; the INI key keeps its meaning.
		bool excludeWorn = !g_showPlayerWorn.load();
		if (ImGuiMCP::Checkbox("Exclude equipped items", &excludeWorn)) {
			g_showPlayerWorn = !excludeWorn;
			SaveFlag(kWindowSection, kShowPlayerWornKey, !excludeWorn);
			logger::info("ShowPlayerWornPieces = {} (SKSE Menu Framework)", !excludeWorn);
		}
		ImGuiMCP::SetItemTooltip("Leaves out items you're wearing unless you carry another copy.\nDefault: ON");
		bool protectFavorites = g_protectFavorites.load();
		if (ImGuiMCP::Checkbox("Exclude favorites", &protectFavorites)) {
			g_protectFavorites = protectFavorites;
			SaveFlag(kTransferSection, kProtectFavoritesKey, protectFavorites);
			logger::info("ProtectFavorites = {} (SKSE Menu Framework)", protectFavorites);
		}
		ImGuiMCP::SetItemTooltip("Excludes items marked as favorites. Quest items are always excluded.\nDefault: ON");
		ImGuiMCP::EndDisabled();
		ImGuiMCP::Unindent();

		SettingGap();

		// Takes effect the next time the window opens.
		bool showSlots = g_showSlotColumn.load();
		if (ImGuiMCP::Checkbox("Show Slot column", &showSlots)) {
			g_showSlotColumn = showSlots;
			SaveFlag(kWindowSection, kShowSlotColumnKey, showSlots);
			logger::info("ShowSlotColumn = {} (SKSE Menu Framework)", showSlots);
		}
		ImGuiMCP::SetItemTooltip("Shows each item's body slot numbers (e.g., 32, 34).\nDefault: OFF");

		Heading("Appearance");

		RenderWindowSkin();

		SettingGap();

		// Applies at once.
		bool menuSounds = g_menuSounds.load();
		if (ImGuiMCP::Checkbox("Play menu sounds", &menuSounds)) {
			Settings::SetMenuSounds(menuSounds, kMenuSource);
		}
		ImGuiMCP::SetItemTooltip("Plays the game's menu sounds for clicks in Livery.\nDefault: ON");

		Heading("System");

		RenderRestoreDefaults();

		SettingGap();

		RenderReleaseEveryone();
	}
}

namespace Settings
{
	void Load()
	{
		// The game's working directory is the Skyrim folder (MO2's virtual Data included).
		// An absolute path keeps the profile API from searching the Windows folder.
		g_iniPath = std::filesystem::current_path() / L"Data/SKSE/Plugins/Livery.ini";

		// The INI is not shipped with the plugin (see kIniKeys).
		std::error_code ec;
		if (!std::filesystem::exists(g_iniPath, ec)) {
			WriteIniTemplate();
		} else {
			AddMissingIniKeys();
		}

		// A missing file or key keeps the default (on).
		wchar_t value[8]{};
		::GetPrivateProfileStringW(kIniSection, kRestrictKey, L"1", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_followersAndSpouseOnly = std::wstring_view{ value } != L"0";
		logger::info("RestrictToFollowersAndSpouse = {}", g_followersAndSpouseOnly.load());

		// A missing key keeps the default (on).
		::GetPrivateProfileStringW(kIniSection, kLivePreviewKey, L"1", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_livePreview = std::wstring_view{ value } != L"0";
		logger::info("LivePreview = {}", g_livePreview.load());

		// A missing key keeps the default (on).
		::GetPrivateProfileStringW(kIniSection, kNpcReactionKey, L"1", value, static_cast<DWORD>(std::size(value)), g_iniPath.c_str());
		g_npcReaction = std::wstring_view{ value } != L"0";
		logger::info("NpcReaction = {}", g_npcReaction.load());

		// The palette picks the skin; [UI] Skin of earlier versions is no longer read.
		wchar_t chosen[MAX_PATH]{};
		::GetPrivateProfileStringW(kUiSection, kPaletteKey, kDefaultPalette, chosen, static_cast<DWORD>(std::size(chosen)), g_iniPath.c_str());
		Palettes::Load(kPaletteDir, chosen, L"Livery");

		// A missing key keeps the default (on).
		::GetPrivateProfileStringW(kUiSection, kMenuSoundsKey, L"1", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_menuSounds = std::wstring_view{ value } != L"0";
		logger::info("MenuSounds = {}", g_menuSounds.load());

		// An out-of-range key or an unknown modifier keeps the default (Shift + X); 0 is no modifier.
		const auto key = ::GetPrivateProfileIntW(kHotkeySection, kHotkeyKeyKey, kDefaultHotkey, g_iniPath.c_str());
		if (key > Key::kNone && key <= 0xFF) {
			g_hotkey = key;
		}
		const auto modifier = ::GetPrivateProfileIntW(kHotkeySection, kHotkeyModifierKey, kDefaultModifier, g_iniPath.c_str());
		if (modifier == kNoModifier) {
			g_modifier = kNoModifier;
		} else if (const auto* known = FindModifier(modifier)) {
			g_modifier = known->left;
		}
		logger::info("Hotkey = {} ({}, modifier {})", HotkeyName(), g_hotkey.load(), g_modifier.load());

		// A missing or unreadable value keeps the default; one out of range is brought into it.
		wchar_t percent[16]{};
		::GetPrivateProfileStringW(kCameraSection, kSpeedPercentKey, L"", percent, static_cast<DWORD>(std::size(percent)),
			g_iniPath.c_str());
		wchar_t*    end = nullptr;
		const float read = std::wcstof(percent, &end);
		if (end != percent) {
			g_speedPercent = std::clamp(read, kMinSpeedPercent, kMaxSpeedPercent);
		}
		logger::info("SpeedPercent = {:.0f}", g_speedPercent.load());

		// A missing key keeps the default (off).
		::GetPrivateProfileStringW(kPlayerSection, kAddMissingKey, L"0", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_playerAddsMissing = std::wstring_view{ value } == L"1";
		logger::info("AddMissingPieces = {}", g_playerAddsMissing.load());

		// A missing key keeps the default.
		::GetPrivateProfileStringW(kWindowSection, kShowPlayerInventoryKey, L"1", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_showPlayerInventory = std::wstring_view{ value } != L"0";
		::GetPrivateProfileStringW(kWindowSection, kShowPlayerWornKey, L"0", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_showPlayerWorn = std::wstring_view{ value } == L"1";
		::GetPrivateProfileStringW(kTransferSection, kProtectFavoritesKey, L"1", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_protectFavorites = std::wstring_view{ value } != L"0";
		::GetPrivateProfileStringW(kTransferSection, kMoveFromPlayerKey, L"1", value, static_cast<DWORD>(std::size(value)), g_iniPath.c_str());
		g_moveFromPlayer = std::wstring_view{ value } != L"0";
		logger::info("ShowPlayerInventory = {}, ShowPlayerWornPieces = {}, ProtectFavorites = {}, MoveFromPlayer = {}", g_showPlayerInventory.load(),
			g_showPlayerWorn.load(), g_protectFavorites.load(), g_moveFromPlayer.load());

		// A missing key keeps the default (off).
		::GetPrivateProfileStringW(kWindowSection, kShowSlotColumnKey, L"0", value, static_cast<DWORD>(std::size(value)), g_iniPath.c_str());
		g_showSlotColumn = std::wstring_view{ value } == L"1";
		logger::info("ShowSlotColumn = {}", g_showSlotColumn.load());

		// A missing key keeps the default (off).
		::GetPrivateProfileStringW(kSituationsSection, kNotifyChangesKey, L"0", value, static_cast<DWORD>(std::size(value)), g_iniPath.c_str());
		g_notifySituationChanges = std::wstring_view{ value } == L"1";
		logger::info("NotifyChanges = {}", g_notifySituationChanges.load());

		// A missing key keeps the default (off).
		::GetPrivateProfileStringW(kUninstallSection, kReleaseEveryoneKey, L"0", value, static_cast<DWORD>(std::size(value)), g_iniPath.c_str());
		g_releaseEveryoneOnLoad = std::wstring_view{ value } == L"1";
		logger::info("ReleaseEveryoneOnLoad = {}", g_releaseEveryoneOnLoad.load());
	}

	void RegisterMenu(void (*a_releaseEveryone)())
	{
		// The module handle, not IsInstalled() (a file check), tells whether the DLL actually loaded.
		if (!GetMenuFrameworkModule()) {
			logger::info("SKSE Menu Framework not loaded; settings are INI only");
			return;
		}
		g_releaseEveryone = a_releaseEveryone;
		SKSEMenuFramework::SetSection("Livery");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		// Registered for the life of the process; the framework owns the returned object.
		SKSEMenuFramework::AddInputEvent(OnInput);
		logger::info("Settings page added to SKSE Menu Framework");
	}

	bool IsHotkey(std::uint32_t a_scanCode)
	{
		if (a_scanCode != g_hotkey.load()) {
			return false;
		}
		const auto modifier = g_modifier.load();
		if (modifier == kNoModifier) {
			return true;
		}
		const auto* known = FindModifier(modifier);
		return known && IsModifierHeld(*known);
	}

	bool FollowersAndSpouseOnly()
	{
		return g_followersAndSpouseOnly.load();
	}

	bool LivePreview()
	{
		return g_livePreview.load();
	}

	bool NpcReaction()
	{
		return g_npcReaction.load();
	}

	Theme::Skin Skin()
	{
		return Palettes::Skin();
	}

	Theme::Palette Palette()
	{
		return Palettes::Palette();
	}

	PaletteChoices ListPalettes()
	{
		return Palettes::List();
	}

	void ChoosePalette(std::size_t a_index, const char* a_source)
	{
		if (const auto stem = Palettes::Choose(a_index, a_source)) {
			SavePalette(*stem);
		}
	}

	void RestoreChosenPalette(const char* a_source)
	{
		Palettes::RestoreChosen(a_source);
	}

	bool MenuSounds()
	{
		return g_menuSounds.load();
	}

	void SetMenuSounds(bool a_on, const char* a_source)
	{
		g_menuSounds = a_on;
		SaveMenuSounds(a_on);
		logger::info("MenuSounds = {} ({})", a_on, a_source);
	}

	float CameraSpeedScale()
	{
		return g_speedPercent.load() / 100.0f;
	}

	bool PlayerAddsMissingPieces()
	{
		return g_playerAddsMissing.load();
	}

	bool ShowPlayerInventory()
	{
		return g_showPlayerInventory.load();
	}

	bool ShowPlayerWornPieces()
	{
		return g_showPlayerWorn.load();
	}

	bool ProtectFavorites()
	{
		return g_protectFavorites.load();
	}

	bool ShowSlotColumn()
	{
		return g_showSlotColumn.load();
	}

	bool MoveFromPlayer()
	{
		return g_moveFromPlayer.load();
	}

	bool NotifySituationChanges()
	{
		return g_notifySituationChanges.load();
	}

	bool ReleaseEveryoneOnLoad()
	{
		return g_releaseEveryoneOnLoad.load();
	}
}
