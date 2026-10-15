// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keep imgui.h out of this file: SKSEMenuFramework.h defines IM_COL32 and other
// ImGui macros of its own. Its ImGuiMCP calls draw with SKSE Menu Framework's
// ImGui context, not the one Hooks.cpp creates.
#include "PCH.h"

#include "Settings.h"

#include <array>
#include <atomic>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <SKSEMenuFramework.h>

namespace
{
	constexpr auto kIniSection = L"General";
	constexpr auto kRestrictKey = L"RestrictToFollowersAndSpouse";
	constexpr auto kUiSection = L"UI";
	constexpr auto kSkinKey = L"Skin";

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
	constexpr std::array<IniKey, 2> kIniKeys{ {
		{ kIniSection, kRestrictKey, L"1",
			L"1 = F8 captures only followers and your spouse; anyone else shows an error message.\n"
			L"0 = F8 captures any adult NPC.\n"
			L"Read once when the game starts." },
		{ kUiSection, kSkinKey, L"Tailor",
			L"Skin of the capture window: Tailor, Vanilla, Nordic or Classic.\n"
			L"Read once when the game starts." },
	} };

	struct SkinOption
	{
		Theme::Skin    skin;
		const char*    label;    // SKSE Menu Framework and the log
		const wchar_t* iniName;  // [UI] Skin value
	};

	// In SKSE Menu Framework order; the first is the default.
	constexpr std::array<SkinOption, 4> kSkinOptions{ {
		{ Theme::Skin::kTailor, "Tailor", L"Tailor" },
		{ Theme::Skin::kVanilla, "Vanilla", L"Vanilla" },
		{ Theme::Skin::kNordic, "Nordic", L"Nordic" },
		{ Theme::Skin::kClassic, "Classic", L"Classic" },
	} };

	std::atomic<bool>        g_followersAndSpouseOnly{ true };
	std::atomic<Theme::Skin> g_skin{ kSkinOptions[0].skin };
	std::filesystem::path    g_iniPath;  // set once by Load, read-only afterwards

	const SkinOption& OptionOf(Theme::Skin a_skin)
	{
		for (const auto& option : kSkinOptions) {
			if (option.skin == a_skin) {
				return option;
			}
		}
		return kSkinOptions[0];
	}

	// --- INI ------------------------------------------------------------------

	// Writes a fresh INI holding every key with its comment and default.
	void WriteIniTemplate()
	{
		std::wofstream file{ g_iniPath };
		if (!file) {
			logger::warn("Could not create TailorOutfitCapture.ini");
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
		logger::info("Created TailorOutfitCapture.ini with default settings");
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
				logger::warn("Could not write TailorOutfitCapture.ini (error {})", ::GetLastError());
				return;
			}
			logger::info("Added missing INI key [{}] {}", SKSE::stl::utf16_to_utf8(entry.section).value_or(""),
				SKSE::stl::utf16_to_utf8(entry.key).value_or(""));
		}
	}

	void SaveFollowersAndSpouseOnly(bool value)
	{
		if (!::WritePrivateProfileStringW(kIniSection, kRestrictKey, value ? L"1" : L"0", g_iniPath.c_str())) {
			logger::warn("Could not write TailorOutfitCapture.ini (error {})", ::GetLastError());
		}
	}

	void SaveSkin(Theme::Skin a_skin)
	{
		if (!::WritePrivateProfileStringW(kUiSection, kSkinKey, OptionOf(a_skin).iniName, g_iniPath.c_str())) {
			logger::warn("Could not write TailorOutfitCapture.ini (error {})", ::GetLastError());
		}
	}

	// --- SKSE Menu Framework page ---------------------------------------------

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
	}
}

namespace Settings
{
	void Load()
	{
		// The game's working directory is the Skyrim folder (MO2's virtual Data included).
		// An absolute path keeps the profile API from searching the Windows folder.
		g_iniPath = std::filesystem::current_path() / L"Data/SKSE/Plugins/TailorOutfitCapture.ini";

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

		// A missing or unknown value keeps the default (Tailor).
		wchar_t skin[16]{};
		::GetPrivateProfileStringW(kUiSection, kSkinKey, L"", skin, static_cast<DWORD>(std::size(skin)), g_iniPath.c_str());
		for (const auto& option : kSkinOptions) {
			if (::_wcsicmp(skin, option.iniName) == 0) {
				g_skin = option.skin;
			}
		}
		logger::info("Skin = {}", OptionOf(g_skin.load()).label);
	}

	void RegisterMenu()
	{
		// The module handle, not IsInstalled() (a file check), tells whether the DLL actually loaded.
		if (!GetMenuFrameworkModule()) {
			logger::info("SKSE Menu Framework not loaded; settings are INI only");
			return;
		}
		SKSEMenuFramework::SetSection("Tailor Outfit Capture");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		logger::info("Settings page added to SKSE Menu Framework");
	}

	bool FollowersAndSpouseOnly()
	{
		return g_followersAndSpouseOnly.load();
	}

	Theme::Skin Skin()
	{
		return g_skin.load();
	}
}
