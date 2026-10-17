// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

struct ID3D11Device;

// Look of the capture window (see Theme.cpp). Keep imgui.h out of this header:
// Settings.cpp includes it next to SKSEMenuFramework.h, whose ImGui macros clash.
namespace Theme
{
	enum class Skin
	{
		kTailor,   // Tailor 3.0's colours, spacing and fonts
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

	// A field label: grey in Nordic, plain text elsewhere. Opens a field with
	// SectionGap and sits a little closer to the control below it than the
	// item spacing would.
	void LabelText(const char* text);

	// Extra room above a field, on top of the item spacing; LabelText adds it
	// itself, so call it only before a field that has no label.
	void SectionGap();

	// Returns true when pressed. Nordic draws a pointed-end frame.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Button(const char* label);

	// Width Button(label) will take, so a caller can centre a row of them.
	float ButtonWidth(const char* label);

	// A list of rows between a rule above and a rule below. The rules and the
	// rows' fills span the content; the rows' click areas run on past the
	// window's padding to its edges (in a table cell, only on the table's outer
	// sides, never into the gap between columns), and their text keeps that
	// padding as an indent. Used like
	// BeginChild / EndChild: always call EndList, before the next BeginList.
	// Rows go through TickRow, SelectRow or plain text.
	// Contract: a table holding lists needs ImGuiTableFlags_NoClip, or its
	// columns clip the outer sides short of the window's edges.
	bool BeginList(const char* id, float height);
	void EndList();

	// Pixels on the back buffer: the padding between a list's rules and its
	// rows, and the gap between rows. The padding is the piece list's row gap
	// in every list; the gap is Tailor's tight one, or the given one otherwise.
	float ListPaddingY();
	float ListRowGap(float fallback);

	// A row the user ticks on and off; returns true when toggled. While ticked,
	// Tailor fills it faintly amber with an amber rule on its left edge, as
	// Tailor 3.0 marks a selected row; Vanilla and Nordic draw it like a SkyUI
	// inventory row: the skin's equipped mark and a white bar fading in from the
	// left.
	// Contract: inside BeginList; the label is shown as is and is the row's
	// ImGui ID, so PushID around rows whose labels may repeat.
	bool TickRow(const char* label, bool* ticked);

	// A row of a single-choice list, SelectRowHeight tall: filled while selected
	// or hovered, its text a little right of the list's indent; returns true
	// when clicked. A selected row is marked left of its text: NORDIC UI's MCM
	// page arrow in Vanilla and Nordic, Tailor's amber rule on its left edge.
	// Lay rows out SelectRowGap apart (pixels on the back buffer).
	// Contract: inside BeginList; the label is shown as is and is the row's
	// ImGui ID, so PushID around rows whose labels may repeat.
	bool  SelectRow(const char* label, bool selected);
	float SelectRowHeight();
	float SelectRowGap();

	// A checkbox; returns true when toggled. Vanilla and Nordic draw the game's
	// settings checkbox (ImGui's own when its texture is missing), Tailor a
	// smaller rounded ImGui one. Faded and inert inside BeginDisabled, like
	// ImGui::Checkbox.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Checkbox(const char* label, bool* value);

	// A radio button; returns true when pressed, and the caller selects it.
	// Vanilla and Nordic draw it as their checkbox, Tailor (or a missing
	// texture) as ImGui's own.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool RadioButton(const char* label, bool active);

	// How a field's frame stands out: kHighlighted draws its border in the
	// skin's accent (the tick's colour) while it narrows a list; kError draws
	// the border and the text in ErrorColor.
	enum class FieldLook
	{
		kNormal,
		kHighlighted,
		kError
	};

	// ImGui::InputText with the skin's field border (fainter in Vanilla and
	// Nordic, as dropdowns) unless `look` says otherwise. `flags` are
	// ImGuiInputTextFlags. `hint` is shown faded inside the empty field.
	bool InputText(const char* id, char* buffer, std::size_t size, int flags, const char* hint = nullptr, FieldLook look = FieldLook::kNormal);

	// The red of error text and fields, packed as ImGui's ImU32.
	unsigned ErrorColor();

	// The game's ticked checkbox in the current window, its right edge at
	// `right` and centred on `midY` (screen pixels). Returns false, drawing
	// nothing, when the skin is not Vanilla or Nordic or the textures are
	// missing, so the caller draws its own mark.
	bool DrawTickedBox(float right, float midY);

	// A dropdown, used like ImGui::BeginCombo / EndCombo but always paired:
	// fill the popup only when BeginCombo returned true, then call EndCombo
	// with that result and the same preview. Every skin gives the popup its own
	// padding. Tailor replaces ImGui's arrow with a small amber chevron; Vanilla
	// and Nordic replace it with the game's diamond stepper arrow (drawn, with
	// the preview, by EndCombo), or keep ImGui's when its texture is missing,
	// and draw the frame's and popup's border fainter.
	// `flags` are ImGuiComboFlags. `highlighted` draws the frame's border as
	// FieldLook::kHighlighted does.
	bool BeginCombo(const char* id, const char* preview, int flags, bool highlighted = false);
	void EndCombo(bool open, const char* preview);

	// A frame-high square button drawn as a cross, grey until hovered; returns
	// true when clicked. Faded and inert inside BeginDisabled. Lay it over the
	// right end of a field submitted with ImGui::SetNextItemAllowOverlap, or
	// beside one.
	bool ClearButton(const char* id);
}
