// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// User settings from Data/SKSE/Plugins/TailorOutfitCapture.ini, editable in game
// through SKSE Menu Framework when it is installed (see Settings.cpp).
namespace Settings
{
	// Contract: call once from SKSEPluginLoad, before any other function here.
	void Load();

	// Contract: call at kPostLoad, when every SKSE plugin (SKSE Menu Framework
	// included) is loaded. Does nothing when SKSE Menu Framework is not loaded.
	void RegisterMenu();

	// Safe from any thread.
	bool FollowersAndSpouseOnly();
}
