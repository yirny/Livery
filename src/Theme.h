// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

struct ID3D11Device;

// Look of the capture window (see Theme.cpp). Keep imgui.h out of this header:
// Settings.cpp includes it next to SKSEMenuFramework.h, whose ImGui macros clash.
namespace Theme
{
	enum class Skin
	{
		kTailor,   // Tailor 3.0's colours, spacing and fonts
		kClassic,  // Dear ImGui's default dark style
		kVanilla,  // the game's own menus
		kNordic    // NORDIC UI
	};

	// Contract: render thread, once, after ImGui::CreateContext and before the
	// first frame. The font data stays in memory for the whole process.
	void LoadFonts();

	// Contract: render thread, once, on the game's D3D11 device. Loads the
	// Nordic skin's ornament textures; they live as long as the process.
	void LoadTextures(ID3D11Device* device);

	// Contract: render thread, before ImGui::NewFrame. Rebuilds the whole style
	// for the skin and scales it to the back buffer height in pixels.
	void Apply(Skin skin, float displayHeight);

	// The widgets below draw in the active skin, between Apply and Render.
	// Nordic decorates them; the other skins draw plain ImGui widgets.

	// False when the skin draws its window without a title bar (Nordic).
	bool HasTitleBar();

	// Call right after ImGui::Begin: corner ornaments (Nordic).
	void DecorateWindow();

	// One line in the heading font; Nordic centres it over an ornamented rule.
	void HeadingText(const char* text);

	// A field label: grey in Nordic, plain text elsewhere.
	void LabelText(const char* text);

	// Returns true when pressed. Nordic draws a pointed-end frame.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Button(const char* label);
}
