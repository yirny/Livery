// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

struct ID3D11Device;

// Look of the capture window (see Theme.cpp). Keep imgui.h out of this header:
// Settings.cpp includes it next to SKSEMenuFramework.h, whose ImGui macros clash.
namespace Theme
{
	enum class Skin
	{
		kModern,   // dark panels in a palette's colours, nearly square frames after Edge UI (it began as Tailor 3.0's look)
		kVanilla,  // the game's own menus
		kNordic    // NORDIC UI
	};

	// A skin's colours, each 0xRRGGBBAA (AA: opacity, FF opaque). The defaults are the Modern
	// palette's, a dark grey; Settings holds Vanilla's and Nordic's and reads the palette files.
	// Vanilla uses every colour but accentLight, Nordic neither accentLight, titleBar nor selected.
	struct Palette
	{
		std::uint32_t window = 0x1A1B1CF0;       // the window
		std::uint32_t surface = 0x2A2D2EFA;      // popups, dropdown lists and tooltips
		std::uint32_t titleBar = 0x0A0B0CFF;     // the title bars of the window and its popups
		std::uint32_t text = 0xE4E7E8FF;         // text
		std::uint32_t label = 0x989B9CFF;        // field and column labels, list headings, links, greyed text
		std::uint32_t accent = 0xA0A3A4FF;       // ticks, a selected row's rule, lit links, the dropdown arrow, a narrowing field's frame
		std::uint32_t accentLight = 0xD0D3D4FF;  // the text cursor, a scroll bar or slider held
		std::uint32_t rule = 0x4A4B4CFF;         // the rules and frames of the window, its fields and lists
		std::uint32_t hover = 0x4B4E4FFF;        // a hovered row
		std::uint32_t selected = 0x2A2D2EFF;     // a selected row, the selected tab, a narrowing field's fill
		std::uint32_t error = 0xFF6666FF;        // error text and fields
	};

	// Contract: render thread, once, after ImGui::CreateContext and before the
	// first frame. The font data stays in memory for the whole process.
	void LoadFonts();

	// Contract: render thread, once, on the game's D3D11 device. Loads the
	// Vanilla and Nordic skins' textures and the piece icons (PieceIcons::FindFiles);
	// they live as long as the process.
	void LoadTextures(ID3D11Device* device);

	// Contract: render thread, before ImGui::NewFrame. Rebuilds the whole style
	// for the skin in `palette`'s colours and scales it to the back buffer
	// height in pixels.
	void Apply(Skin skin, const Palette& palette, float displayHeight);

	// The widgets below draw in the active skin, between Apply and Render; each one's comment
	// says what a skin draws in place of ImGui's own.

	// False when the skin draws its main window without a title bar (Vanilla, Nordic), or its
	// popups (Nordic).
	bool HasTitleBar();
	bool HasPopupTitleBar();

	// Call right after ImGui::Begin, or BeginPopupModal with `popup`: corner ornaments (Nordic);
	// on the main window, Vanilla's frame, a 3px band of the rule colour inside its edge and a
	// 4px band of the window's glass outside it, in place of ImGui's border.
	void DecorateWindow(bool popup = false);

	// One line in the heading font, then the subtitle (if any) greyed close under
	// it; Nordic centres both over an ornamented rule.
	void HeadingText(const char* text, const char* subtitle = nullptr);

	// A page's heading made of text tabs in place of HeadingText: BeginHeadingTabs with every
	// label in order, then TextTab for each with HeadingTabSeparator between them, then
	// EndHeadingTabs. The tabs are in the heading font, a greyed "/" between them, on one line
	// where HeadingText puts its text (Nordic: centred, over its rule), with its gap below.
	// HeadingTabsWidth is the line's width.
	// Contract: always pair Begin and End; a tab's tooltip goes after EndHeadingTabs, or it is
	// drawn in the heading font.
	void  BeginHeadingTabs(std::span<const char* const> labels);
	void  HeadingTabSeparator();
	void  EndHeadingTabs();
	float HeadingTabsWidth(std::span<const char* const> labels);

	// Pixels at 1080p to pixels on the current back buffer.
	float Scale();

	// A field is a label (or none) and the control or list it names; a list's count line
	// under it belongs to the list's field. LabelText is a field label in the label font (the
	// small font; in Nordic 0.7 of the body font, 14px), in ASCII capitals, in a grey: the
	// palette's label colour. The help marks and
	// column labels on a label's line, and
	// list headings, take the same font. It
	// opens its field with SectionGap and sits a little closer to the control below it than the
	// item spacing would. `opensField` false, a count line under a list: a smaller gap above
	// it, 6px. CountText is LabelText with the text kept as written and in the small font in
	// every skin, for count lines; the links take the small font too.
	// Contract: every field label goes through LabelText, and every count line
	// ("2 of 5 item(s) selected", "12 outfit(s) in the wardrobe") through CountText.
	void LabelText(const char* text, bool opensField = true);
	void CountText(const char* text, bool opensField = true);

	// A clickable label at the right end of the line just laid out (call it
	// right after CountText): a filled rounded square with a plus cut out of
	// it, then `label`, both in CountText's font and colour, lit while hovered
	// (the text colour in Vanilla and Nordic, the accent in Modern). Returns true when clicked.
	// Inside BeginDisabled it is faded, never lit, and inert, like ImGui's widgets.
	// Contract: the label is shown as is and is the ImGui ID, so it must not use "##" IDs.
	bool LabelLink(const char* label);

	// A greyed "(?)" right after the label just laid out (call it right after LabelText), in
	// LabelText's font, keeping its line's gap below. Returns true while hovered, for the
	// caller's tooltip.
	bool LabelHelpMark();

	// LabelLink without the plus mark, `label` followed inside its click area by a greyed
	// "(?)": text only at the right end of the label line just laid out (call it right after
	// LabelText), in the small font, lit while hovered. `*hovered` tells the caller to show the help. Returns
	// true when clicked. Faded and inert inside BeginDisabled.
	// Contract: the label is shown as is and is the ImGui ID.
	bool LabelHelpLink(const char* label, bool* hovered);

	// LabelHelpLink on a control's line (call it right after the control and whatever sits
	// beside it): at the line's right end, its text centred on the line's height.
	// Contract: the label is shown as is and is the ImGui ID.
	bool LineHelpLink(const char* label, bool* hovered);

	// LineHelpLink without the "(?)": `label` alone at the line's right end, lit while hovered.
	// Contract: the label is shown as is and is the ImGui ID.
	bool LineLink(const char* label);

	// Width LineHelpLink(label) takes, so a caller can keep room for it beside a centred row.
	float LineHelpLinkWidth(const char* label);

	// Extra room above a field, on top of the item spacing: 6px in Modern, 8px in Vanilla and
	// Nordic. LabelText adds it itself, so call it only before a field that has no label.
	void SectionGap();

	// Returns true when pressed. Nordic draws a pointed-end frame; Vanilla its label alone, 2px
	// larger than the body font, in LabelText's grey and the text colour while hovered or held, with 2px
	// clear on either side.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Button(const char* label);

	// Width Button(label) will take, so a caller can centre a row of them.
	float ButtonWidth(const char* label);

	// Height Button and HelpButton take: the frame height, in Vanilla in its larger font.
	float ButtonHeight();

	// Button with a greyed "(?)" after its label, inside its frame; `*hovered` tells the
	// caller to show the help. It is as wide as Button(label + " (?)").
	// Contract: the label is shown as is and is the ImGui ID.
	bool HelpButton(const char* label, bool* hovered);

	// A square-cornered list of rows between a rule above and a rule below (none with
	// `rules` false). The rules and the rows' fills span the content. In Modern the whole list keeps
	// to the content, its backdrop ending at the window's padding as the other
	// controls do; in Vanilla and Nordic the rows' click areas run on past the
	// window's padding to its edges (Vanilla's right one to its frame's band, so a
	// scrollbar stays off the band; in a table cell, only on the table's outer
	// sides, never into the gap between columns), and their text keeps that
	// padding as an indent. Used like
	// BeginChild / EndChild: always call EndList, before the next BeginList.
	// Rows go through TickRow, SelectRow or TextRow. Vanilla and Nordic draw the game's scroll
	// bar (arrows at its ends, its track, a thumb in the style's grab colours) over ImGui's,
	// which still takes the clicks; without its textures, ImGui's own. In every skin the bar
	// is hidden until a wheel turn over the list, the mouse over the bar or a drag of its thumb
	// fades it in (0.2 s); it stays while the mouse is in the list and fades out (0.8 s) once
	// the mouse has been out of it for 0.1 s.
	// Contract: a table holding lists needs ImGuiTableFlags_NoClip, or its
	// columns clip the outer sides short of the window's edges (Vanilla, Nordic).
	bool BeginList(const char* id, float height, bool rules = true);
	void EndList();

	// Pixels on the back buffer: the padding between a list's rules and its
	// rows, and the gap between rows. The padding is the piece list's row gap
	// in every list; the gap is Modern's tight one, or the given one otherwise.
	float ListPaddingY();
	float ListRowGap(float fallback);

	// Columns of a header row and the tick rows under it: each column's left edge
	// in pixels from where a tick row's text starts, the first at 0 (RowIconRoom when the
	// rows have icons), ascending. A
	// cell's text ends, with an ellipsis, ColumnGap() short of the next column, and
	// the last column's at the row's right end.
	//
	// Room for a tick row's columns, from where its text starts to the list's
	// right end. Contract: called outside BeginList, at the list's indent.
	float ColumnsWidth();

	// Pixels on the back buffer between the end of one column's text and the next column.
	float ColumnGap();

	// The column labels over a list, in LabelText's font, colour and capitals, each lit
	// while hovered (the text colour in Vanilla and Nordic, the accent in Modern); the label of
	// column `sorted` has a small arrow after it, pointing up unless
	// `descending`. Opens a field as LabelText does. Returns the index of the
	// column clicked, or -1. Faded and inert inside BeginDisabled.
	// Contract: right above BeginList, one label per column; the labels are shown
	// as is and are ImGui IDs.
	int HeaderRow(std::span<const char* const> labels, std::span<const float> columnX, int sorted, bool descending);

	// A row the user ticks on and off, one text per column; returns true when
	// toggled. While ticked, Modern fills it in the palette's selected colour, full at the
	// row's centre, 0.6 of it at the left end and fading to nothing at the right end, with an
	// accent rule on its left edge, as Tailor 3.0 marks a selected row; Vanilla and Nordic
	// draw it like a SkyUI inventory row: the skin's equipped mark, and in Vanilla a bar in the
	// selected colour fading in from the left. Hovered: Modern's hover colour, graded as the ticked fill, Vanilla's soft
	// ellipse and NORDIC UI's selection line under the row in the hover colour.
	// With `icon`, the icon's texture is drawn where the row's text starts, centred on the row,
	// tinted; a key with no texture draws nothing.
	// Contract: inside BeginList, one cell per column; the cells are shown as is
	// and the first is the row's ImGui ID, so PushID around rows whose first cells
	// may repeat. Rows with icons start their first column at RowIconRoom.
	struct RowIcon
	{
		std::string_view key;      // PieceIcons' key; "" for none
		std::uint32_t    rgb = 0;  // the white icon's tint, 0xRRGGBB
	};
	bool TickRow(std::span<const char* const> cells, std::span<const float> columnX, bool* ticked, const RowIcon& icon = {});

	// Pixels on the back buffer a tick row's icon takes left of its first column: the icon,
	// 0.8 of the row's height square, and a 6px gap; 0 when no icon texture loaded.
	float RowIconRoom();

	// A row of a single-choice list, SelectRowHeight tall, its text a little right of the
	// list's indent; returns true when clicked. A selected row is marked left of its text:
	// NORDIC UI's MCM page arrow in Vanilla and Nordic, Modern's accent rule on its left edge;
	// Modern also fills it as a ticked row, Vanilla lays the tick row's bar over it,
	// Nordic shows the arrow alone. A hovered row gets the tick row's hover fill.
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

	// A mark left of a list heading's text, an 18px square in its grey centred on the text's
	// line, which moves the text right.
	enum class HeadingIcon
	{
		kNone,
		kBackpack,     // the Inventory tab's backpack
		kDressedForm,  // an outfit row's mark of the outfit worn now
		kBareForm      // an outfit row's mark of the Default, the Manage Wardrobe tab's form
	};

	// A heading inside a list: a TextRow in LabelText's font, greyed, in capitals (the piece
	// list's groups), with `icon` before the text and, 12px on, a list heading rule to 4px short
	// of the list's right end. ListHeadingHeight is its height.
	// Contract: ListHeading inside BeginList.
	void  ListHeading(const char* text, HeadingIcon icon = HeadingIcon::kNone);
	float ListHeadingHeight();

	// ListHeading (in capitals) with a greyed "(?)" after the text, the rule after the mark; returns
	// true while the mouse is over that mark alone, for the caller's tooltip. `situationIndent`: the
	// text starts where a SituationHeading's name does, as over Manage Situations' headings, with
	// no rule.
	// Contract: inside BeginList.
	bool ListHeadingHelp(const char* text, bool situationIndent = false, HeadingIcon icon = HeadingIcon::kNone);

	// Extra room under a field whose next line is a label (a dropdown, then a list's count
	// line), on top of the label's own SectionGap.
	void AfterFieldGap();

	// Where an outfit row's drag grip goes, if it has one: two columns of three dots, its name
	// 12px past them.
	enum class Grip
	{
		kNone,
		kEdge,  // where a FoldHeading's text starts, after its chevron (Manage Situations' outfit list)
		kName   // 14px in from the row's left edge (the Situation List, level with its headings' names)
	};

	// A list heading rule (FoldHeading, SituationHeading): in Vanilla and Nordic, SkyUI's MCM
	// header rule in ListHeading's grey, a knot at its left end and a double line on to its
	// closed right end, 0.7 times LabelText's font size tall; in Modern, or without its texture
	// or room for its two ends, a 1px line in the list's rule colour.

	// A sorted outfit list's heading, FoldHeadingHeight tall, that folds the rows under it
	// away: a chevron pointing down while `open` and right while folded, then the text in
	// capitals, both in ListHeading's grey and lit while hovered (the text colour in Vanilla and Nordic, the accent in Modern),
	// then, 12px on, a list heading rule to 4px short of the list's right end. The whole row is
	// the click target; returns true when clicked, and the caller folds or unfolds.
	// In LabelText's font. Its chevron starts at the list's left edge, the text after it, where
	// the grip of a Grip::kEdge row starts.
	// Contract: inside BeginList, laid out with no item spacing, as the outfit rows are; the
	// text is shown as is and is the row's ImGui ID.
	bool  FoldHeading(const char* text, bool open);
	float FoldHeadingHeight();

	// Which part of a SituationHeading was clicked.
	enum class HeadingClick
	{
		kNone,
		kSelect,  // left of the chevron
		kFold,    // the chevron, with its padding, to Edit
		kEdit     // Edit, with its padding, to the list's right end
	};

	struct SituationHeadingLook
	{
		int  count = -1;        // shown as "(count)" after the name; -1 for none
		bool random = false;    // a die between the name and the count: the outfit is picked at random
		bool open = true;       // the chevron points down; folded, right
		bool foldable = true;   // it has rows to fold: the chevron and its click area show
		bool selected = false;  // marked and filled as a selected SelectRow
		bool dim = false;       // the name greyed
	};

	// A situation's heading in Manage Situations' Situation List, SituationHeadingHeight tall:
	// `name` 14px in from its left edge, level with the rows' grip dots under it (Grip::kName),
	// the select mark left of it; with `random` a die after it, a square
	// 4px short of its line height, greyed, the die's outline ItemInnerSpacing.x from the name and from the
	// count; its count after it in the small font, greyed, then, while foldable, a chevron, greyed and
	// lit while hovered, then, 12px on, a list heading rule, and 12px on `Edit` at
	// the list's right end, in the small font, greyed and lit while hovered. A hovered one left
	// of the chevron gets the row's hover fill.
	// Contract: inside BeginList, laid out with no item spacing; PushID around each heading.
	HeadingClick SituationHeading(const char* name, const SituationHeadingLook& look);
	float        SituationHeadingHeight();

	// One row of a dropdown's popup, filled and hovered as SelectRow is; returns true when
	// clicked, having toggled `*ticked`. `checkbox` makes it a row of a multi-choice dropdown:
	// Vanilla and Nordic draw the game's checkbox at the row's start (Modern, or a missing
	// texture, a check mark at its right end while ticked). Nordic marks a ticked row with the
	// select arrow left of all that. DropdownRowWidth is the width it needs for `label`.
	// Contract: in the popup; the label is shown as is and is the row's ImGui ID.
	bool  DropdownRow(const char* label, bool* ticked, bool checkbox);
	float DropdownRowWidth(const char* label, bool checkbox);

	// The marks at the right end of an outfit row's name line, in LabelText's grey, each
	// with a tooltip while the mouse is over it: a dressed form for the outfit the actor
	// wears now (`Current outfit`, and `Default Outfit` on a second line when it is their
	// Default too), else a bare form for their Default (`Default Outfit`); a star for one
	// chosen for a situation (`situations` is its tooltip, `For Town, Warm`; null for none).
	struct OutfitMarks
	{
		bool        current = false;
		bool        defaultOutfit = false;  // the actor's Default holds it
		const char* situations = nullptr;
	};

	// A SelectRow with the outfit's name, ending in an ellipsis short of `marks`.
	// The name starts 22px in from the row's left edge in every skin, and a `striped`
	// row (every other one) has a faint white under its fills. With `grip`, a drag grip where
	// it says, in LabelText's grey at low alpha, and
	// the name starts 12px past its dots instead. Selected, Vanilla marks it with the equipped
	// mark of a ticked piece and Nordic with the select arrow, 2px right of the
	// centre of the room left of the name, or centred left of the dots with Grip::kName; with Grip::kEdge in the dots' place,
	// the dots hidden, shrunk to leave 2px on either side.
	// OutfitRowHeight is its height, which holds the row gap: lay rows out with
	// no item spacing, so the stripes meet.
	// Contract: OutfitRow inside BeginList; the name is shown as is and is the row's ImGui
	// ID, so PushID around rows whose names may repeat.
	bool  OutfitRow(const char* name, bool selected, const OutfitMarks& marks, bool striped, Grip grip = Grip::kNone);
	float OutfitRowHeight();

	// A grey line in an outfit row's place, such as an empty list's hint: OutfitRowHeight tall,
	// the text where OutfitRow puts a name without a grip, with no fill or click.
	// Contract: inside BeginList, laid out with no item spacing, as the outfit rows are.
	void HintRow(const char* text);

	// A see-through copy of the outfit row just drawn, shown while it is dragged: the popup
	// background with the grip's dots and the name, no fills or marks in any skin, held where
	// the row was grabbed and following the mouse above every window.
	// Contract: right after the OutfitRow it copies, inside that row's BeginDragDropSource opened
	// with ImGuiDragDropFlags_SourceNoPreviewTooltip, so its list is still the current window.
	void OutfitDragGhost(const char* name, Grip grip);

	// A checkbox; returns true when toggled. Vanilla and Nordic draw the game's
	// settings checkbox (ImGui's own when its texture is missing), Modern a
	// square as tall as the text with a thin frame and tick. Faded and inert
	// inside BeginDisabled, like ImGui::Checkbox.
	// Contract: the label is shown as is, so it must not use "##" IDs.
	bool Checkbox(const char* label, bool* value);

	// A radio button; returns true when pressed, and the caller selects it.
	// Vanilla and Nordic draw it as their checkbox, Modern as ImGui's own the
	// size of its checkbox (a missing texture: ImGui's own).
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
		kBoxed,       // the skin's frame in Modern; Vanilla and Nordic draw it kUnderlined, a text field with a pen at its right end
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
		bool       alignRight = false;     // dropdowns: the preview's text ends 6px short of the arrow
	};

	// Height of a field of this style in the current font (pixels on the back buffer).
	float FieldHeight(const FieldStyle& style);

	// ImGui::InputText drawn in `style`. `flags` are ImGuiInputTextFlags.
	// `hint` is shown faded inside the empty field.
	bool InputText(const char* id, char* buffer, std::size_t size, int flags, const char* hint = nullptr, const FieldStyle& style = {});

	// The red of error text and fields, packed as ImGui's ImU32.
	unsigned ErrorColor();

	// A dropdown, used like ImGui::BeginCombo / EndCombo but always paired:
	// fill the popup only when BeginCombo returned true, then call EndCombo
	// with that result, the preview and the same style; EndCombo draws the
	// preview. Every skin gives the popup its own padding. Modern replaces
	// ImGui's arrow with a small chevron in the accent; Vanilla and Nordic replace it
	// with the game's diamond stepper arrow, grey and white while hovered or open,
	// or keep ImGui's when its texture is missing, and draw the popup's border fainter.
	// The popup's scroll bar is BeginList's, with its fade. `flags` are ImGuiComboFlags.
	bool BeginCombo(const char* id, int flags, const FieldStyle& style = {});
	void EndCombo(bool open, const char* preview, const FieldStyle& style = {});

	// Width a dropdown of this style needs to show `preview` whole, in the current font.
	float ComboWidth(const char* preview, const FieldStyle& style);

	// A square button `side` pixels across drawn as a cross, grey until
	// hovered; returns true when clicked. Faded and inert inside BeginDisabled.
	// Lay it over the right end of a field submitted with
	// ImGui::SetNextItemAllowOverlap, or beside one, as tall as the field.
	bool ClearButton(const char* id, float side);

	// One tab of a row of text-only tabs: its label in the current font (the heading font
	// between BeginHeadingTabs and EndHeadingTabs), in full colour while selected or hovered and
	// half transparent otherwise. Returns true when clicked. Faded
	// and inert inside BeginDisabled.
	// Contract: the label is shown as is and is the ImGui ID.
	bool TextTab(const char* label, bool selected);

	// The tab column's icons, drawn in lines, so they scale with the resolution and take
	// the skin's colours.
	enum class TabIcon
	{
		kInventory,  // a backpack
		kOutfits,    // a bare dress form: a padded torso on a stand
		kSituations  // the star, point down, that marks an outfit chosen for a situation (OutfitMarks)
	};

	// One tab of the column at the window's left edge, a square: the icon in the text colour
	// on the selected row's fill while selected (its corners rounded 2px), else in LabelText's
	// grey and lit while hovered (the text colour in Vanilla and Nordic, the accent in Modern), with
	// `name` as a tooltip. Returns true when clicked. Faded and
	// inert inside BeginDisabled.
	// Contract: `name` is the ImGui ID.
	bool Tab(TabIcon icon, const char* name, bool selected);

	// From the content's left edge, where the tabs start, to where the page beside them
	// starts: the tabs, a gap, the rule and the window's padding.
	float TabColumnWidth();

	// Where the tabs go, in pixels from the content's top-left corner: each tab's left edge,
	// so it is centred between the window's left edge (Vanilla: its frame's band) and the
	// rule (negative, into the window's padding), and the first tab's top (Nordic: below the
	// corner knot).
	float TabOffsetX();
	float TabTopGap();

	// The rule between the tab column, whose tabs start at `left`, and the page, from `top`
	// to `bottom` (screen pixels; Vanilla stops it 2px short of its frame's band at both
	// ends): Modern's 1px rule colour, Vanilla's 2px rule, NORDIC UI's level-up rope (1px of
	// its rule colour without the texture).
	void TabRule(float left, float top, float bottom);

	// A faint 1px vertical rule centred on `x` between two columns, 4px short of `top` and
	// `bottom` (screen pixels): the rule colour at half its alpha in Vanilla and Nordic; nothing
	// in Modern.
	void ColumnRule(float x, float top, float bottom);

	// The body font at a smaller size, for controls that sit beside others
	// (the outfit browser's). Pair them in the same window: ImGui checks that a
	// window ends with the font stack it began with.
	void PushSmallFont();
	void PopSmallFont();
}
