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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SKSEMenuFramework.h>

namespace
{
	constexpr auto kIniSection = L"General";
	constexpr auto kRestrictKey = L"RestrictToFollowersAndSpouse";
	constexpr auto kLivePreviewKey = L"LivePreview";
	constexpr auto kUiSection = L"UI";
	constexpr auto kSkinKey = L"Skin";
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

	constexpr auto kPaletteDir = L"Data/SKSE/Plugins/Livery/palettes";
	constexpr auto kPaletteSection = L"Palette";  // the section of a palette file that holds its colours
	constexpr auto kDefaultPalette = L"Edge";     // keep in step with kIniKeys

	constexpr float kDefaultSpeedPercent = 100.0f;  // keep in step with kIniKeys
	constexpr float kMinSpeedPercent = 10.0f;
	constexpr float kMaxSpeedPercent = 200.0f;

	using Key = RE::BSKeyboardDevice::Keys::Key;
	constexpr std::uint32_t kDefaultHotkey = Key::kF10;  // keep in step with kIniKeys
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
	constexpr std::array<IniKey, 15> kIniKeys{ {
		{ kIniSection, kRestrictKey, L"1",
			L"1 = the hotkey captures only followers and your spouse; anyone else shows an error message.\n"
			L"0 = the hotkey captures any adult NPC.\n"
			L"Read once when the game starts." },
		{ kIniSection, kLivePreviewKey, L"1",
			L"1 = the capture window opens with Live Preview on: the game keeps running and the character wears the ticked pieces.\n"
			L"0 = it opens with Live Preview off and the game paused; tick Live Preview in the window to turn it on.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kUiSection, kSkinKey, L"Modern",
			L"Skin of the capture window: Modern, Vanilla or Nordic. Tailor, from earlier versions, reads as Modern.\n"
			L"Read once when the game starts." },
		{ kUiSection, kPaletteKey, L"Edge",
			L"Colours of the Modern skin: a file in Data/SKSE/Plugins/Livery/palettes, named without .ini (Edge, Norden, or your own copy of one).\n"
			L"Easiest to change in game through SKSE Menu Framework. The palette files are read once when the game starts." },
		{ kUiSection, kMenuSoundsKey, L"1",
			L"1 = the capture window plays the game's menu sounds when it opens and when you click its buttons, ticks and lists.\n"
			L"0 = it stays silent.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kHotkeySection, kHotkeyKeyKey, L"68",
			L"Key that captures the NPC under the crosshair, or you when there is none, as a DirectInput scan code (68 = F10).\n"
			L"Easiest to change in game through SKSE Menu Framework. Read once when the game starts." },
		{ kHotkeySection, kHotkeyModifierKey, L"0",
			L"Key that must be held together with Key: 0 = none, 42 = Shift, 29 = Ctrl, 56 = Alt.\n"
			L"Left and right keys count the same. Read once when the game starts." },
		{ kCameraSection, kSpeedPercentKey, L"100",
			L"Speed of the preview camera: W / A / S / D movement, mouse drags and wheel zoom, as a percentage (10 to 200, 100 = normal).\n"
			L"Shift halves the movement while held. Easiest to change in game through SKSE Menu Framework." },
		{ kPlayerSection, kAddMissingKey, L"0",
			L"1 = an outfit you give yourself adds the pieces you do not carry. Warning: this creates new items in your inventory.\n"
			L"0 = those pieces are left out, and the message on screen names them.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kWindowSection, kShowPlayerInventoryKey, L"1",
			L"1 = the capture window of an NPC also lists your armor, under Player's Inventory.\n"
			L"0 = it lists only the NPC's own pieces and those of the loaded outfit.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kWindowSection, kShowPlayerWornKey, L"0",
			L"1 = that list includes the armor you wear.\n"
			L"0 = it leaves out what you wear, unless you carry another copy.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kWindowSection, kShowSlotColumnKey, L"0",
			L"1 = the capture window's piece list has a Slot column: each piece's body slots by number (32, 34).\n"
			L"0 = it shows the Name and Type columns only.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kTransferSection, kMoveFromPlayerKey, L"1",
			L"1 = Apply in an NPC's capture window moves the ticked pieces from your inventory to them, enchantments and tempering included.\n"
			L"0 = the NPC gets a new plain copy of each and yours stay with you.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kTransferSection, kProtectFavoritesKey, L"1",
			L"1 = your favorites are left out of that list, as quest items always are.\n"
			L"0 = your favorites are listed too.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
		{ kSituationsSection, kNotifyChangesKey, L"0",
			L"1 = a message on screen tells you when a character changes outfit because a situation (Town, Warm, ...) began or ended.\n"
			L"0 = they change without a message.\n"
			L"Easiest to change in game through SKSE Menu Framework." },
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

	struct SkinOption
	{
		Theme::Skin    skin;
		const char*    label;    // SKSE Menu Framework and the log
		const wchar_t* iniName;  // [UI] Skin value
	};

	// In SKSE Menu Framework order; the first is the default.
	constexpr std::array<SkinOption, 3> kSkinOptions{ {
		{ Theme::Skin::kModern, "Modern", L"Modern" },
		{ Theme::Skin::kVanilla, "Vanilla", L"Vanilla" },
		{ Theme::Skin::kNordic, "Nordic", L"Nordic" },
	} };

	std::atomic<bool>          g_followersAndSpouseOnly{ true };
	std::atomic<bool>          g_livePreview{ true };
	std::atomic<Theme::Skin>   g_skin{ kSkinOptions[0].skin };
	std::atomic<bool>          g_menuSounds{ true };
	std::atomic<std::uint32_t> g_hotkey{ kDefaultHotkey };
	std::atomic<std::uint32_t> g_modifier{ kNoModifier };  // left-hand code of a kModifiers entry, or kNoModifier
	std::atomic<bool>          g_pickingHotkey{ false };   // the settings page waits for the next key
	std::atomic<float>         g_speedPercent{ kDefaultSpeedPercent };
	std::atomic<bool>          g_playerAddsMissing{ false };
	std::atomic<bool>          g_showPlayerInventory{ true };
	std::atomic<bool>          g_showPlayerWorn{ false };
	std::atomic<bool>          g_showSlotColumn{ false };
	std::atomic<bool>          g_protectFavorites{ true };
	std::atomic<bool>          g_moveFromPlayer{ true };
	std::atomic<bool>          g_notifySituationChanges{ false };
	std::filesystem::path      g_iniPath;   // set once by Load, read-only afterwards
	void (*g_releaseEveryone)() = nullptr;  // set once by RegisterMenu, before the page can render

	// A palette file of the Modern skin.
	struct PaletteOption
	{
		std::wstring   stem;  // the file name without .ini, as [UI] Palette holds it
		std::string    name;  // the same in UTF-8, for SKSE Menu Framework and the log
		Theme::Palette palette;
	};

	// Contract: filled once by Load, before the render thread or SKSE Menu Framework reads
	// it, and never changed afterwards, so any thread may read it; the choice is atomic.
	std::vector<PaletteOption> g_palettes;           // sorted by name
	std::atomic<std::size_t>   g_paletteIndex{ 0 };  // into g_palettes

	const SkinOption& OptionOf(Theme::Skin a_skin)
	{
		for (const auto& option : kSkinOptions) {
			if (option.skin == a_skin) {
				return option;
			}
		}
		return kSkinOptions[0];
	}

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

	void SaveSkin(Theme::Skin a_skin)
	{
		if (!::WritePrivateProfileStringW(kUiSection, kSkinKey, OptionOf(a_skin).iniName, g_iniPath.c_str())) {
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

	// --- Palettes -------------------------------------------------------------

	// A palette file's keys, each a colour of Theme::Palette.
	struct PaletteKey
	{
		const wchar_t* key;
		std::uint32_t Theme::Palette::* color;
	};
	constexpr std::array<PaletteKey, 11> kPaletteKeys{ {
		{ L"Window", &Theme::Palette::window },
		{ L"Surface", &Theme::Palette::surface },
		{ L"TitleBar", &Theme::Palette::titleBar },
		{ L"Text", &Theme::Palette::text },
		{ L"Label", &Theme::Palette::label },
		{ L"Accent", &Theme::Palette::accent },
		{ L"AccentLight", &Theme::Palette::accentLight },
		{ L"Rule", &Theme::Palette::rule },
		{ L"Hover", &Theme::Palette::hover },
		{ L"Selected", &Theme::Palette::selected },
		{ L"Error", &Theme::Palette::error },
	} };

	// "#RRGGBB" or "#RRGGBBAA" as 0xRRGGBBAA, opaque when AA is left out; nullopt for anything else.
	std::optional<std::uint32_t> ParseColor(std::wstring_view a_text)
	{
		constexpr std::size_t kRgbLength = 7;   // "#RRGGBB"
		constexpr std::size_t kRgbaLength = 9;  // "#RRGGBBAA"

		if (a_text.empty() || a_text.front() != L'#' || (a_text.size() != kRgbLength && a_text.size() != kRgbaLength)) {
			return std::nullopt;
		}
		std::uint32_t value = 0;
		for (const wchar_t c : a_text.substr(1)) {
			std::uint32_t digit = 0;
			if (c >= L'0' && c <= L'9') {
				digit = c - L'0';
			} else if (c >= L'a' && c <= L'f') {
				digit = c - L'a' + 10;
			} else if (c >= L'A' && c <= L'F') {
				digit = c - L'A' + 10;
			} else {
				return std::nullopt;
			}
			value = (value << 4) | digit;
		}
		return a_text.size() == kRgbLength ? (value << 8) | 0xFF : value;
	}

	// A palette file's colours; a key left out, or one that is not a colour (logged), keeps
	// the Edge palette's colour (Theme::Palette's defaults).
	Theme::Palette ReadPalette(const std::filesystem::path& a_path, std::string_view a_name)
	{
		Theme::Palette palette;
		wchar_t        value[32]{};
		for (const auto& entry : kPaletteKeys) {
			const auto length = ::GetPrivateProfileStringW(kPaletteSection, entry.key, L"", value, static_cast<DWORD>(std::size(value)), a_path.c_str());
			if (length == 0) {
				continue;
			}
			if (const auto color = ParseColor(std::wstring_view{ value, length })) {
				palette.*entry.color = *color;
			} else {
				logger::warn("Palette {}: {} = {} is not #RRGGBB or #RRGGBBAA; it keeps Edge's colour", a_name,
					SKSE::stl::utf16_to_utf8(entry.key).value_or(""), SKSE::stl::utf16_to_utf8(value).value_or(""));
			}
		}
		return palette;
	}

	// The palette whose file is named `a_stem` (ASCII case ignored), if any.
	std::optional<std::size_t> FindPalette(std::wstring_view a_stem)
	{
		for (std::size_t i = 0; i < g_palettes.size(); ++i) {
			if (::_wcsicmp(g_palettes[i].stem.c_str(), std::wstring{ a_stem }.c_str()) == 0) {
				return i;
			}
		}
		return std::nullopt;
	}

	// Reads every .ini in the palettes folder, then picks the one [UI] Palette names, else
	// Edge, else the first. With no palette file, Settings::Palette is Edge's built-in colours.
	// Contract: called once by Settings::Load (see g_palettes).
	void LoadPalettes()
	{
		const auto      folder = std::filesystem::current_path() / kPaletteDir;
		std::error_code ec;
		for (std::filesystem::directory_iterator it{ folder, ec }, end; !ec && it != end; it.increment(ec)) {
			const auto& path = it->path();
			if (!it->is_regular_file(ec) || ::_wcsicmp(path.extension().c_str(), L".ini") != 0) {
				continue;
			}
			auto stem = path.stem().wstring();
			auto name = SKSE::stl::utf16_to_utf8(stem).value_or("?");
			auto palette = ReadPalette(path, name);
			g_palettes.push_back({ std::move(stem), std::move(name), palette });
		}
		std::ranges::sort(g_palettes, [](const PaletteOption& a_left, const PaletteOption& a_right) {
			return ::_wcsicmp(a_left.stem.c_str(), a_right.stem.c_str()) < 0;
		});
		logger::info("Loaded {} palette(s) from {}", g_palettes.size(), folder.string());
		if (g_palettes.empty()) {
			logger::info("Palette = Edge (built in)");
			return;
		}

		wchar_t chosen[MAX_PATH]{};
		::GetPrivateProfileStringW(kUiSection, kPaletteKey, kDefaultPalette, chosen, static_cast<DWORD>(std::size(chosen)), g_iniPath.c_str());
		auto index = FindPalette(chosen);
		if (!index) {
			index = FindPalette(kDefaultPalette);
			logger::warn("No palette file named {}; using {}", SKSE::stl::utf16_to_utf8(chosen).value_or("?"), g_palettes[index.value_or(0)].name);
		}
		g_paletteIndex = index.value_or(0);
		logger::info("Palette = {}", g_palettes[g_paletteIndex.load()].name);
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

	// The uninstall step, asked once more in a popup since it undoes every assignment.
	// Contract: render thread (RenderSettings); g_releaseEveryone only queues an SKSE task,
	// which checks whether the release can run now.
	void RenderReleaseEveryone()
	{
		constexpr auto kPopup = "Release everyone?###ReleaseEveryone";
		if (ImGuiMCP::Button("Release everyone")) {
			ImGuiMCP::OpenPopup(kPopup);
		}
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

	// Contract: SKSE Menu Framework calls this on its render thread every frame
	// the page is shown. The INI is written only when the player clicks.
	void __stdcall RenderSettings()
	{
		bool value = g_followersAndSpouseOnly.load();
		if (ImGuiMCP::Checkbox("Only followers and your spouse can be captured", &value)) {
			g_followersAndSpouseOnly = value;
			SaveFollowersAndSpouseOnly(value);
			logger::info("RestrictToFollowersAndSpouse = {} (SKSE Menu Framework)", value);
		}

		// Two Spacing() calls separate the settings; they scale with the framework's font.
		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Takes effect the next time the capture window opens.
		bool livePreview = g_livePreview.load();
		if (ImGuiMCP::Checkbox("Live Preview on when the capture window opens", &livePreview)) {
			g_livePreview = livePreview;
			SaveLivePreview(livePreview);
			logger::info("LivePreview = {} (SKSE Menu Framework)", livePreview);
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Takes effect the next time the capture window opens.
		ImGuiMCP::Text("Window skin");
		const auto skin = g_skin.load();
		for (const auto& option : kSkinOptions) {
			if (&option != &kSkinOptions[0]) {
				ImGuiMCP::SameLine();
			}
			if (ImGuiMCP::RadioButton(option.label, skin == option.skin) && skin != option.skin) {
				g_skin = option.skin;
				SaveSkin(option.skin);
				logger::info("Skin = {} (SKSE Menu Framework)", option.label);
			}
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Takes effect the next time the capture window opens, in the Modern skin.
		if (!g_palettes.empty()) {
			const auto current = g_paletteIndex.load();
			if (ImGuiMCP::BeginCombo("Modern skin palette", g_palettes[current].name.c_str())) {
				for (std::size_t i = 0; i < g_palettes.size(); ++i) {
					ImGuiMCP::PushID(static_cast<int>(i));
					if (ImGuiMCP::Selectable(g_palettes[i].name.c_str(), i == current) && i != current) {
						g_paletteIndex = i;
						SavePalette(g_palettes[i].stem);
						logger::info("Palette = {} (SKSE Menu Framework)", g_palettes[i].name);
					}
					ImGuiMCP::PopID();
				}
				ImGuiMCP::EndCombo();
			}
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Applies at once.
		bool menuSounds = g_menuSounds.load();
		if (ImGuiMCP::Checkbox("Play menu sounds", &menuSounds)) {
			g_menuSounds = menuSounds;
			SaveMenuSounds(menuSounds);
			logger::info("MenuSounds = {} (SKSE Menu Framework)", menuSounds);
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// The next key press is taken by OnInput; "###" keeps the button's ID while its text changes.
		ImGuiMCP::Text("Capture hotkey");
		ImGuiMCP::SameLine();
		const bool picking = g_pickingHotkey.load();
		const auto label = picking ? std::string{ "Press a key (Esc cancels)###Hotkey" } : HotkeyName() + "###Hotkey";
		if (ImGuiMCP::Button(label.c_str())) {
			g_pickingHotkey = !picking;
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Applies at once; the INI is written when the slider is let go.
		float percent = g_speedPercent.load();
		if (ImGuiMCP::SliderFloat("Preview camera speed", &percent, kMinSpeedPercent, kMaxSpeedPercent, "%.0f%%")) {
			g_speedPercent = std::clamp(percent, kMinSpeedPercent, kMaxSpeedPercent);
		}
		if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
			SaveSpeedPercent(g_speedPercent.load());
			logger::info("SpeedPercent = {:.0f} (SKSE Menu Framework)", g_speedPercent.load());
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Takes effect the next time you dress yourself.
		bool addMissing = g_playerAddsMissing.load();
		if (ImGuiMCP::Checkbox("Dressing yourself adds the pieces you do not carry (creates new items)", &addMissing)) {
			g_playerAddsMissing = addMissing;
			SavePlayerAddsMissing(addMissing);
			logger::info("AddMissingPieces = {} (SKSE Menu Framework)", addMissing);
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// The three below take effect the next time the capture window opens.
		bool showInventory = g_showPlayerInventory.load();
		if (ImGuiMCP::Checkbox("An NPC's capture window lists your armor too", &showInventory)) {
			g_showPlayerInventory = showInventory;
			SaveFlag(kWindowSection, kShowPlayerInventoryKey, showInventory);
			logger::info("ShowPlayerInventory = {} (SKSE Menu Framework)", showInventory);
		}
		bool showWorn = g_showPlayerWorn.load();
		if (ImGuiMCP::Checkbox("... including the armor you wear", &showWorn)) {
			g_showPlayerWorn = showWorn;
			SaveFlag(kWindowSection, kShowPlayerWornKey, showWorn);
			logger::info("ShowPlayerWornPieces = {} (SKSE Menu Framework)", showWorn);
		}
		bool protectFavorites = g_protectFavorites.load();
		if (ImGuiMCP::Checkbox("... leaving out your favorites", &protectFavorites)) {
			g_protectFavorites = protectFavorites;
			SaveFlag(kTransferSection, kProtectFavoritesKey, protectFavorites);
			logger::info("ProtectFavorites = {} (SKSE Menu Framework)", protectFavorites);
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Takes effect the next time the capture window opens.
		bool showSlots = g_showSlotColumn.load();
		if (ImGuiMCP::Checkbox("The capture window's piece list shows a Slot column", &showSlots)) {
			g_showSlotColumn = showSlots;
			SaveFlag(kWindowSection, kShowSlotColumnKey, showSlots);
			logger::info("ShowSlotColumn = {} (SKSE Menu Framework)", showSlots);
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Takes effect at the next Apply.
		bool moveFromPlayer = g_moveFromPlayer.load();
		if (ImGuiMCP::Checkbox("Apply moves your pieces to the NPC (off: the NPC gets new copies)", &moveFromPlayer)) {
			g_moveFromPlayer = moveFromPlayer;
			SaveFlag(kTransferSection, kMoveFromPlayerKey, moveFromPlayer);
			logger::info("MoveFromPlayer = {} (SKSE Menu Framework)", moveFromPlayer);
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

		// Applies at once.
		bool notifyChanges = g_notifySituationChanges.load();
		if (ImGuiMCP::Checkbox("Show a message when a character changes outfit for a situation", &notifyChanges)) {
			g_notifySituationChanges = notifyChanges;
			SaveFlag(kSituationsSection, kNotifyChangesKey, notifyChanges);
			logger::info("NotifyChanges = {} (SKSE Menu Framework)", notifyChanges);
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Spacing();

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

		// A missing or unknown value keeps the default (Modern), as Tailor does: the skin Modern replaced.
		wchar_t skin[16]{};
		::GetPrivateProfileStringW(kUiSection, kSkinKey, L"", skin, static_cast<DWORD>(std::size(skin)), g_iniPath.c_str());
		for (const auto& option : kSkinOptions) {
			if (::_wcsicmp(skin, option.iniName) == 0) {
				g_skin = option.skin;
			}
		}
		logger::info("Skin = {}", OptionOf(g_skin.load()).label);
		LoadPalettes();

		// A missing key keeps the default (on).
		::GetPrivateProfileStringW(kUiSection, kMenuSoundsKey, L"1", value, static_cast<DWORD>(std::size(value)),
			g_iniPath.c_str());
		g_menuSounds = std::wstring_view{ value } != L"0";
		logger::info("MenuSounds = {}", g_menuSounds.load());

		// An out-of-range key or an unknown modifier keeps the default (F10, no modifier).
		const auto key = ::GetPrivateProfileIntW(kHotkeySection, kHotkeyKeyKey, kDefaultHotkey, g_iniPath.c_str());
		if (key > Key::kNone && key <= 0xFF) {
			g_hotkey = key;
		}
		const auto modifier = ::GetPrivateProfileIntW(kHotkeySection, kHotkeyModifierKey, kNoModifier, g_iniPath.c_str());
		if (const auto* known = FindModifier(modifier)) {
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

	Theme::Skin Skin()
	{
		return g_skin.load();
	}

	const Theme::Palette& Palette()
	{
		static const Theme::Palette builtIn;  // Edge's colours, when no palette file was found
		const auto                  index = g_paletteIndex.load();
		return index < g_palettes.size() ? g_palettes[index].palette : builtIn;
	}

	bool MenuSounds()
	{
		return g_menuSounds.load();
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
}
