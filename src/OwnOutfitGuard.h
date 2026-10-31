// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Keeps an assigned NPC's own outfit items from taking their locked pieces off (docs/design.md
// §2, Keeping the outfit on), by hooking the call that puts those items on in the engine's
// worn-gear pass, the one SPID hooks for its distributed outfits. Skyrim VR and AE before 1.6.640
// are not covered: Install() logs that and returns.
namespace OwnOutfitGuard
{
	// The call is checked against its byte signature first; one that does not match is left alone.
	// Contract: call once at kDataLoaded. SPID hooks the same call at kPostLoad and its hook never
	// calls what it replaced, so this one has to go in after it, and calls it in turn.
	void Install();
}
