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
	// Vanilla and Nordic skins' textures; they live as long as the process.
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

	// One line in the heading font, then the subtitle (if any) greyed close under
	// it; Nordic centres both over an ornamented rule.
	void HeadingText(const char* text, const char* subtitle = nullptr);

	// Pixels at 1080p to pixels on the current back buffer.
	float Scale();

	// A field label: grey in Nordic, plain text elsewhere. Sits a little closer
	// to the control below it than the item spacing would.
	void LabelText(const char* text);

	// Returns true when pressed. Nordic draws a pointed-end frame.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Button(const char* label);

	// Width Button(label) will take, so a caller can centre a row of them.
	float ButtonWidth(const char* label);

	// A row the user ticks on and off; returns true when toggled. Vanilla and
	// Nordic draw it like a SkyUI inventory row: the skin's equipped mark and a
	// white bar fading in from the left while ticked. The other skins draw a checkbox.
	// Contract: the label is shown as is and is the row's ImGui ID, so PushID
	// around rows whose labels may repeat.
	bool TickRow(const char* label, bool* ticked);

	// A checkbox; returns true when toggled. Vanilla and Nordic draw the game's
	// settings checkbox, the other skins (or a missing texture) ImGui's own.
	// Faded and inert inside BeginDisabled, like ImGui::Checkbox.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Checkbox(const char* label, bool* value);

	// The game's ticked checkbox in the current window, its right edge at
	// `right` and centred on `midY` (screen pixels). Returns false, drawing
	// nothing, when the skin is not Vanilla or Nordic or the textures are
	// missing, so the caller draws its own mark.
	bool DrawTickedBox(float right, float midY);
}
