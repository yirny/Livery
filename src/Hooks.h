// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Engine hooks that give the plugin an ImGui overlay (see Hooks.cpp).
// Skyrim VR is not supported: Install() is a no-op there and IsRenderReady()
// stays false, so callers fall back to a UI-less path.
namespace Hooks
{
	// Call once from SKSEPluginLoad, after SKSE::Init allocated the trampoline.
	void Install();

	// True once ImGui has been initialised on the game's D3D11 device.
	bool IsRenderReady();
}
