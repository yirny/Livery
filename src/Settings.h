// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "Theme.h"

// User settings from Data/SKSE/Plugins/Livery.ini, editable in game
// through SKSE Menu Framework when it is installed (see Settings.cpp). The INI is
// created with defaults on first start; keys a newer version adds are appended.
namespace Settings
{
	// Contract: call once from SKSEPluginLoad, before any other function here.
	void Load();

	// The page ends with Release everyone, whose popup's Yes calls `a_releaseEveryone` on
	// SKSE Menu Framework's render thread.
	// Contract: call at kPostLoad, when every SKSE plugin (SKSE Menu Framework
	// included) is loaded. Does nothing when SKSE Menu Framework is not loaded.
	void RegisterMenu(void (*a_releaseEveryone)());

	// Safe from any thread.
	bool FollowersAndSpouseOnly();

	// Whether the capture window opens with Live Preview on. Safe from any thread.
	bool LivePreview();

	// Whether a captured NPC turns their head to the player and says a greeting (NpcReaction)
	// when the window opens with Live Preview on. Safe from any thread.
	bool NpcReaction();

	// Safe from any thread.
	Theme::Skin Skin();

	// The Modern skin's palette that [UI] Palette or SKSE Menu Framework chose, from the files in
	// Data/SKSE/Plugins/Livery/palettes, which Load reads once. Safe from any thread.
	const Theme::Palette& Palette();

	// Whether the capture window plays the game's menu sounds. Safe from any thread.
	bool MenuSounds();

	// Multiplier on the preview camera's movement, mouse drags and wheel zoom; 1 is normal. Safe from any thread.
	float CameraSpeedScale();

	// Whether dressing the player adds the pieces they do not carry, which creates items. Safe from any thread.
	bool PlayerAddsMissingPieces();

	// What an NPC's capture window lists of the player's armor: any at all, also the pieces the
	// player wears, and whether favorites stay out (quest items always do). Safe from any thread.
	bool ShowPlayerInventory();
	bool ShowPlayerWornPieces();
	bool ProtectFavorites();

	// Whether the capture window's piece list has the Slot column; off, Name and Type only.
	// Read when the window opens. Safe from any thread.
	bool ShowSlotColumn();

	// Whether Apply in an NPC's window moves the ticked pieces from the player's inventory to
	// the NPC; off, the NPC gets new copies. Safe from any thread.
	bool MoveFromPlayer();

	// Whether a redress for a situation beginning or ending (the situation poll) shows a HUD
	// message. Safe from any thread.
	bool NotifySituationChanges();

	// Whether a keyboard key going down (DirectInput scan code) is the capture hotkey:
	// the configured key, with its modifier held when one is configured.
	// Contract: call from the input path while handling that key's event, since the
	// modifier is read from the keyboard's current state.
	bool IsHotkey(std::uint32_t a_scanCode);
}
