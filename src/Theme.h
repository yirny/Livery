// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string>

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

	// A field label in the small font (PushSmallFont): grey in Vanilla and
	// Nordic, plain text in Tailor. Opens a field with SectionGap and sits a
	// little closer to the control below it than the item spacing would.
	// Contract: every field label goes through here, a list's count line
	// ("2 of 5 piece(s) selected", "12 outfit(s) in the wardrobe") included.
	void LabelText(const char* text);

	// A clickable label at the right end of the line just laid out (call it
	// right after LabelText): a filled rounded square with a plus cut out of
	// it, then `label`, both in LabelText's font and colour, lit while hovered
	// (white in Vanilla and Nordic, amber in Tailor). Returns true when clicked.
	// Inside BeginDisabled it is faded, never lit, and inert, like ImGui's widgets.
	// Contract: the label is shown as is and is the ImGui ID, so it must not use "##" IDs.
	bool LabelLink(const char* label);

	// Extra room above a field, on top of the item spacing; LabelText adds it
	// itself, so call it only before a field that has no label.
	void SectionGap();

	// Returns true when pressed. Nordic draws a pointed-end frame.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Button(const char* label);

	// Width Button(label) will take, so a caller can centre a row of them.
	float ButtonWidth(const char* label);

	// A square-cornered list of rows between a rule above and a rule below. The
	// rules and the rows' fills span the content. In Tailor the whole list keeps
	// to the content, its backdrop ending at the window's padding as the other
	// controls do; in Vanilla and Nordic the rows' click areas run on past the
	// window's padding to its edges (in a table cell, only on the table's outer
	// sides, never into the gap between columns), and their text keeps that
	// padding as an indent. Used like
	// BeginChild / EndChild: always call EndList, before the next BeginList.
	// Rows go through TickRow, SelectRow or TextRow.
	// Contract: a table holding lists needs ImGuiTableFlags_NoClip, or its
	// columns clip the outer sides short of the window's edges (Vanilla, Nordic).
	bool BeginList(const char* id, float height);
	void EndList();

	// Pixels on the back buffer: the padding between a list's rules and its
	// rows, and the gap between rows. The padding is the piece list's row gap
	// in every list; the gap is Tailor's tight one, or the given one otherwise.
	float ListPaddingY();
	float ListRowGap(float fallback);

	// Columns of a header row and the tick rows under it: each column's left edge
	// in pixels from where a tick row's text starts, the first at 0, ascending. A
	// cell's text ends, with an ellipsis, ColumnGap() short of the next column, and
	// the last column's at the row's right end.
	//
	// Room for a tick row's columns, from where its text starts to the list's
	// right end. Contract: called outside BeginList, at the list's indent.
	float ColumnsWidth();

	// Pixels on the back buffer between the end of one column's text and the next column.
	float ColumnGap();

	// The column labels over a list, in LabelText's font and colour, each lit
	// while hovered (white in Vanilla and Nordic, amber in Tailor); the label of
	// column `sorted` has a small arrow after it, pointing up unless
	// `descending`. Opens a field as LabelText does. Returns the index of the
	// column clicked, or -1. Faded and inert inside BeginDisabled.
	// Contract: right above BeginList, one label per column; the labels are shown
	// as is and are ImGui IDs.
	int HeaderRow(std::span<const char* const> labels, std::span<const float> columnX, int sorted, bool descending);

	// A row the user ticks on and off, one text per column; returns true when
	// toggled. While ticked, Tailor fills it faintly amber with an amber rule on
	// its left edge, as Tailor 3.0 marks a selected row; Vanilla and Nordic draw it
	// like a SkyUI inventory row: the skin's equipped mark and a white bar fading
	// in from the left.
	// Contract: inside BeginList, one cell per column; the cells are shown as is
	// and the first is the row's ImGui ID, so PushID around rows whose first cells
	// may repeat.
	bool TickRow(std::span<const char* const> cells, std::span<const float> columnX, bool* ticked);

	// A row of a single-choice list, SelectRowHeight tall: filled while selected
	// or hovered, its text a little right of the list's indent; returns true
	// when clicked. A selected row is marked left of its text: NORDIC UI's MCM
	// page arrow in Vanilla and Nordic, Tailor's amber rule on its left edge.
	// Lay rows out ListRowGap apart, as the tick rows.
	// Contract: inside BeginList; the label is shown as is and is the row's
	// ImGui ID, so PushID around rows whose labels may repeat.
	bool  SelectRow(const char* label, bool selected);
	float SelectRowHeight();

	// A read-only row laid out as SelectRow lays out its text, with no fill or
	// click, so a plain list lines up with a single-choice one beside it.
	// Lay rows out ListRowGap apart.
	// Contract: inside BeginList.
	void TextRow(const char* label);

	// A heading inside a list: a TextRow in the small font, greyed (the piece list's
	// groups, a sorted outfit list's headings). ListHeadingHeight is its height.
	// Contract: ListHeading inside BeginList.
	void  ListHeading(const char* text);
	float ListHeadingHeight();

	// A sorted outfit list's heading, ListHeadingHeight tall, that folds the rows under it
	// away: a chevron pointing down while `open` and right while folded, then the text, both in
	// ListHeading's grey and lit while hovered (white in Vanilla and Nordic, amber in Tailor),
	// then a rule in the list's rule colour to the list's right end. The whole row is the click
	// target; returns true when clicked, and the caller folds or unfolds.
	// Contract: inside BeginList; the text is shown as is and is the row's ImGui ID.
	bool FoldHeading(const char* text, bool open);

	// The fill of a hovered row outside a list (a dropdown's rows), from `left`, `top` to
	// `right`, `bottom` (screen pixels): as a list row's hover, Tailor's muted brown with the
	// frame's rounding, Vanilla's and Nordic's soft white ellipse.
	void HoverFill(float left, float top, float right, float bottom);

	// A SelectRow with the outfit's tags under its name in the small font, wrapping onto
	// more lines within `tagsWidth` (OutfitTagsWidth): its gender first unless unisex (-1
	// unisex, 0 male, 1 female, as the library stores it), then `tags`. Tailor draws each
	// tag as Tailor 3.0's framed chip, the gender as its rose or steel pill; Vanilla and
	// Nordic as grey text, comma separated. With no tag it is laid out as a SelectRow.
	// OutfitRowHeight is its height. Lay rows out ListRowGap apart.
	// Contract: OutfitRow inside BeginList; the name is shown as is and is the row's ImGui
	// ID, so PushID around rows whose names may repeat.
	bool  OutfitRow(const char* name, int gender, std::span<const std::string> tags, float tagsWidth, bool selected);
	float OutfitRowHeight(int gender, std::span<const std::string> tags, float tagsWidth);

	// Room for an outfit row's tags, from where its text starts to the list's right end.
	// Contract: called outside BeginList, at the list's indent, as ColumnsWidth is.
	float OutfitTagsWidth();

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

	// How a field's frame or rule stands out: kHighlighted draws it in the
	// skin's accent (the tick's colour) while the field narrows a list; kError
	// draws it and the text in ErrorColor.
	enum class FieldLook
	{
		kNormal,
		kHighlighted,
		kError
	};

	enum class FieldShape
	{
		kBoxed,       // the skin's frame (fainter in Vanilla and Nordic)
		kUnderlined,  // no frame: one rule under the text, coloured as the frame would be; filled only while highlighted
		kBare         // no fill, frame or rule, and as tall as the text
	};

	// A mark drawn left of a field's text, which moves the text right.
	enum class FieldIcon
	{
		kNone,
		kSearch,  // a magnifying glass
		kFilter   // a funnel
	};

	struct FieldStyle
	{
		FieldLook  look = FieldLook::kNormal;
		FieldShape shape = FieldShape::kBoxed;
		FieldIcon  icon = FieldIcon::kNone;
		bool       previewIsHint = false;  // dropdowns: the preview is greyed, as an empty text field's hint
	};

	// Height of a field of this style in the current font (pixels on the back buffer).
	float FieldHeight(const FieldStyle& style);

	// ImGui::InputText drawn in `style`. `flags` are ImGuiInputTextFlags.
	// `hint` is shown faded inside the empty field.
	bool InputText(const char* id, char* buffer, std::size_t size, int flags, const char* hint = nullptr, const FieldStyle& style = {});

	// The red of error text and fields, packed as ImGui's ImU32.
	unsigned ErrorColor();

	// The game's ticked checkbox in the current window, its right edge at
	// `right` and centred on `midY` (screen pixels). Returns false, drawing
	// nothing, when the skin is not Vanilla or Nordic or the textures are
	// missing, so the caller draws its own mark.
	bool DrawTickedBox(float right, float midY);

	// A dropdown, used like ImGui::BeginCombo / EndCombo but always paired:
	// fill the popup only when BeginCombo returned true, then call EndCombo
	// with that result, the preview and the same style; EndCombo draws the
	// preview. Every skin gives the popup its own padding. Tailor replaces
	// ImGui's arrow with a small amber chevron; Vanilla and Nordic replace it
	// with the game's diamond stepper arrow, or keep ImGui's when its texture
	// is missing, and draw the frame's and popup's border fainter.
	// `flags` are ImGuiComboFlags.
	bool BeginCombo(const char* id, int flags, const FieldStyle& style = {});
	void EndCombo(bool open, const char* preview, const FieldStyle& style = {});

	// Width a dropdown of this style needs to show `preview` whole, in the current font.
	float ComboWidth(const char* preview, const FieldStyle& style);

	// A square button `side` pixels across drawn as a cross, grey until
	// hovered; returns true when clicked. Faded and inert inside BeginDisabled.
	// Lay it over the right end of a field submitted with
	// ImGui::SetNextItemAllowOverlap, or beside one, as tall as the field.
	bool ClearButton(const char* id, float side);

	// Text with no frame in the body font, in LabelText's colour and lit while
	// hovered (white in Vanilla and Nordic, amber in Tailor). Returns true when
	// clicked. Faded and inert inside BeginDisabled.
	// Contract: the label is shown as is and is the ImGui ID.
	bool TextButton(const char* label);

	// One tab of a row of text-only tabs: its label in the body font, in full colour while
	// selected or hovered and half transparent otherwise. Returns true when clicked. Faded
	// and inert inside BeginDisabled.
	// Contract: the label is shown as is and is the ImGui ID.
	bool TextTab(const char* label, bool selected);

	// The tab column's icons, drawn in lines, so they scale with the resolution and take
	// the skin's colours.
	enum class TabIcon
	{
		kInventory,  // a backpack
		kOutfits     // a dress form: a padded torso on a stand
	};

	// One tab of the column at the window's left edge, a square: the icon in the text
	// colour on a faint fill while selected, else in LabelText's grey (Tailor's ghost) and
	// lit while hovered, with `name` as a tooltip. Returns true when clicked. Faded and
	// inert inside BeginDisabled.
	// Contract: `name` is the ImGui ID.
	bool Tab(TabIcon icon, const char* name, bool selected);

	// From the content's left edge, where the tabs start, to where the page beside them
	// starts: the tabs, a gap, the rule and the window's padding.
	float TabColumnWidth();

	// Where the tabs go, in pixels from the content's top-left corner: each tab's left edge,
	// so it is centred between the window's left edge and the rule (negative, into the
	// window's padding), and the first tab's top (Nordic: below the corner knot).
	float TabOffsetX();
	float TabTopGap();

	// The rule between the tab column, whose tabs start at `left`, and the page, from `top`
	// to `bottom` (screen pixels): Tailor's 1px border colour, Vanilla's 2px rule, NORDIC
	// UI's level-up rope (1px of its rule colour without the texture).
	void TabRule(float left, float top, float bottom);

	// The body font at a smaller size, for controls that sit beside others
	// (the outfit browser's). Pair them in the same window: ImGui checks that a
	// window ends with the font stack it began with.
	void PushSmallFont();
	void PopSmallFont();
}
