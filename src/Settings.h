// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sarrai/Theme.h"

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

	// The skin and colours of the palette [UI] Palette or a Window skin dropdown chose, from the
	// files in Data/SKSE/Plugins/Livery/palettes, which Load writes when missing and reads once;
	// Restore original palette rewrites and rereads one. Safe from any thread.
	Theme::Skin    Skin();
	Theme::Palette Palette();

	// What the Window skin dropdowns (the settings page's and the window's Appearance page) show:
	// every palette's label, sorted, the chosen one, its file name without .ini, and whether
	// Restore original palette would change it (a shipped palette whose file differs from what
	// Livery ships). No labels with no palette file. Safe from any thread.
	struct PaletteChoices
	{
		std::vector<std::string> labels;
		std::size_t              chosen = 0;  // into labels
		std::string              file;
		bool                     edited = false;
	};
	PaletteChoices ListPalettes();

	// ChoosePalette chooses the palette at `a_index` into ListPalettes().labels and writes it to
	// [UI] Palette; RestoreChosenPalette writes the chosen shipped palette's file back as Livery
	// ships it and rereads it (a file that cannot be written changes nothing). The capture window
	// draws in the result from its next frame. `a_source` names the page in the log.
	// Contract: a render thread (the settings page's or the window's).
	void ChoosePalette(std::size_t a_index, const char* a_source);
	void RestoreChosenPalette(const char* a_source);

	// Whether the capture window plays the game's menu sounds. Safe from any thread.
	bool MenuSounds();

	// Turns the window's menu sounds on or off and writes [UI] MenuSounds; `a_source` names the
	// page in the log. Safe from any thread.
	void SetMenuSounds(bool a_on, const char* a_source);

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

	// Whether every save load releases everyone Livery dresses ([Uninstall] ReleaseEveryone), the
	// uninstall path without SKSE Menu Framework. Read once at Load, never written back by Livery.
	// Safe from any thread.
	bool ReleaseEveryoneOnLoad();

	// Whether a keyboard key going down (DirectInput scan code) is the capture hotkey:
	// the configured key, with its modifier held when one is configured.
	// Contract: call from the input path while handling that key's event, since the
	// modifier is read from the keyboard's current state.
	bool IsHotkey(std::uint32_t a_scanCode);
}
