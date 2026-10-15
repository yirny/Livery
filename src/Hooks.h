// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Engine hooks and the game menu that give the plugin an ImGui overlay drawn
// under the game's own cursor (see Hooks.cpp). Skyrim VR is not supported:
// Install() and RegisterMenu() are no-ops there and IsRenderReady() stays
// false, so callers fall back to a UI-less path.
namespace Hooks
{
	// Call once from SKSEPluginLoad, after SKSE::Init allocated the trampoline.
	void Install();

	// Call once at kDataLoaded: registers the menu that hosts the window.
	void RegisterMenu();

	// Contract: game thread (CaptureWindow::Open / Close). Queue the menu's
	// show / hide message; the game opens or closes it a frame later.
	void ShowMenu();
	void HideMenu();

	// True once ImGui has been initialised on the game's D3D11 device and the
	// menu is registered.
	bool IsRenderReady();
}
