// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Keeps the copies Livery added to an NPC out of the trade, container and gift menus, where
// the player could otherwise take them for free (docs/design.md §3), by hooking the calls that
// fill a menu's item list from an inventory, the ones SPID hooks for its distributed outfits.
// Skyrim VR and AE before 1.6.1130 are not covered: Install() logs that and returns.
namespace TradeFilter
{
	// Call once from SKSEPluginLoad, after SKSE::Init allocated the trampoline. Every call site
	// is checked against its byte signature first; one that does not match is left alone.
	void Install();
}
