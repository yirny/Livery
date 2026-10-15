// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "Theme.h"

// User settings from Data/SKSE/Plugins/TailorOutfitCapture.ini, editable in game
// through SKSE Menu Framework when it is installed (see Settings.cpp). The INI is
// created with defaults on first start; keys a newer version adds are appended.
namespace Settings
{
	// Contract: call once from SKSEPluginLoad, before any other function here.
	void Load();

	// Contract: call at kPostLoad, when every SKSE plugin (SKSE Menu Framework
	// included) is loaded. Does nothing when SKSE Menu Framework is not loaded.
	void RegisterMenu();

	// Safe from any thread.
	bool FollowersAndSpouseOnly();

	// Safe from any thread.
	Theme::Skin Skin();

	// Whether a keyboard key going down (DirectInput scan code) is the capture hotkey:
	// the configured key, with its modifier held when one is configured.
	// Contract: call from the input path while handling that key's event, since the
	// modifier is read from the keyboard's current state.
	bool IsHotkey(std::uint32_t a_scanCode);
}
