// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Look of the capture window (see Theme.cpp). Keep imgui.h out of this header:
// Settings.cpp includes it next to SKSEMenuFramework.h, whose ImGui macros clash.
namespace Theme
{
	enum class Skin
	{
		kTailor,  // Tailor 3.0's colours, spacing and fonts
		kClassic  // Dear ImGui's default dark style
	};

	// Contract: render thread, once, after ImGui::CreateContext and before the
	// first frame. The font data stays in memory for the whole process.
	void LoadFonts();

	// Contract: render thread, before ImGui::NewFrame. Rebuilds the whole style
	// for the skin and scales it to the back buffer height in pixels.
	void Apply(Skin skin, float displayHeight);

	// Draws one line in the active skin's heading font (between Apply and Render).
	void HeadingText(const char* text);
}
