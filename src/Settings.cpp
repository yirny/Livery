// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keep imgui.h out of this file: SKSEMenuFramework.h defines IM_COL32 and other
// ImGui macros of its own. Its ImGuiMCP calls draw with SKSE Menu Framework's
// ImGui context, not the one Hooks.cpp creates.
#include "PCH.h"

#include "Settings.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <string_view>

#include <SKSEMenuFramework.h>

namespace
{
	constexpr auto kIniSection = L"General";
	constexpr auto kRestrictKey = L"RestrictToFollowersAndSpouse";
	constexpr auto kUiSection = L"UI";
	constexpr auto kSkinKey = L"Skin";

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
