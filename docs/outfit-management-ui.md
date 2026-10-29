# Outfit management UI

Written 2026-10-20 for the AI that implements it, from the user's draft and the decisions
made on it; current spec only. Three steps, each built, checked in game and committed
before the next, each with CLAUDE.md §1 approval first: (1) the outfit list and the
Inventory page's buttons, (2) the tab column and the Wardrobe page with Update,
(3) Remove. Docs follow the code in the step that makes them stale: design.md, README, and
CLAUDE.md §2.1 (step 2 adds the Wardrobe page's Update path, step 3 the window's delete).

The Inventory page is today's Capture page (`Page::kCapture`), titled `#{Name}'s Inventory`.
Docs call it the Inventory page; code identifiers stay.

Window-wide look: field labels, list headings (folding ones included) and column labels
are drawn in ASCII capitals (`Theme`'s `Caps`), the strings in code staying as
written; count lines (`Theme::CountText`), notes, buttons and links keep their case. In
Vanilla and Nordic a page's two columns have a faint 1px rule between them, 4px short of
the columns' top and bottom. Tooltips sit (32, 20)px from the mouse at 1080p
(`style.MouseCursorScale`), clear of the game's cursor. In Vanilla and Nordic the rule after
a list heading's text (the piece list's group headings, folding outfit list headings, Manage
Situations' situation headings; not its `Situation List` heading) is SkyUI's MCM header rule,
knot at its left end, in the headings' grey; lists and dropdown
popups scroll with the game's journal scroll bar, 16px wide at 1080p with 2px clear on
either side: its arrows are the game's art, its track and thumb drawn in code with 1px lines
at any resolution, the thumb's rim and its fill while held in the track's light grey
(#cccccc) (Modern keeps a 1px rule and ImGui's bar). In every skin a list's or popup's scroll
bar is hidden until the wheel turns over the list, the mouse is over the bar or its thumb is
dragged; it fades in over 0.2 s, stays while the mouse is in the list, and fades out over
0.8 s once the mouse has been out of the list for 0.1 s. Fonts at 1080p: Modern's body 20px
and headings 26px (Jost Book and Medium), Vanilla's 22px and 28px (Oswald), Nordic's 20px and
26px (Sovngarde). The palette `[UI] Palette` names picks the skin and its colours
(`Theme::Palette`, read from `Data/SKSE/Plugins/Livery/palettes/*.ini` by sarrai-common's `Palettes`); below,
"the accent", "the selected colour" and "the hover colour" are that palette's. Field labels, their help
marks, column labels and list headings take the label font: the small font (0.85 of the body),
in Nordic 0.7 of the body (14px). Count lines, the links (`Add items from Wardrobe...`,
`Edit`, `Restore`, `Remove`, `Restore Presets`) take the small font in every skin.

## 1. Outfit list (step 1)

The outfit list shared by the Load page, the Save page's Update tab and the Wardrobe page
page. Each page keeps its own search and sort.

- Wording: the window calls the library the wardrobe (`12 outfit(s) in the wardrobe`,
  `Not in Wardrobe`).
- One category group, Situation: the captured actor's rows (`SITUATIONS (#{Name})`) that
  hold the outfit (Default and the situations), from `Request::assigned`. Never stored in
  the library. An older file's outfit categories are read past and gone at the next write.
- Each row: the outfit's icon and name, one line high.
- Hover, in every list and in the dropdowns' rows: Modern fills a hovered row in the hover
  colour, graded as a selected row's fill; Vanilla lights it with a soft white ellipse fading out to the row's edges, as SkyUI
  lights its MCM title bar (a texture made at load, `Theme::LoadTextures`); Nordic draws
  NORDIC UI's list selection line under it (skyui/inventorylists.swf, `ItemsListEntry`'s
  `selectIndicator`, shape 1: a thin wedge pointed at its left end, #e5e5e5 fading from 0.4
  to nothing), from the shape's points in code, stretched across the row, as thick as the
  shape is in a 28-unit entry of the frame height.
- Selected (a single-choice row, a ticked piece, a ticked dropdown row): Modern fills it in
  the selected colour, 0.6 of it at the left end, full at the centre and fading to nothing at the
  right end, with a 2px rule in the accent on its left edge; Vanilla lays the piece list's white bar fading in from the left over it,
  its right end a point 0.4 times the row's height long, as NORDIC UI's inventory bar's;
  Nordic shows no fill. In Vanilla and Nordic a list's fills (hover and selected) stop 6px
  short of its scroll bar while one shows. The mark: NORDIC UI's MCM page arrow on single-choice and dropdown
  rows in Vanilla and Nordic (Nordic's 4px larger, its window padding 32px); the equipped
  mark on piece rows. A selected outfit row (the outfit the preview shows) has Vanilla's
  equipped mark or Nordic's MCM page arrow, centred where the arrow would be (on an outfit
  row without a drag grip, in the equipped mark's column, as on a ticked piece row); Nordic
  puts the arrow on a ticked dropdown row too, at its start.
- A multi-choice dropdown row: Vanilla and Nordic draw the game's checkbox at the row's start
  (after Nordic's arrow), then the label; Modern a check mark at its right end.
- Marks at the right end of each row's name line, in LabelText's grey, the name cut short
  of them: a dressed form (a gown on the stand) for the outfit the actor wears now
  (`Request::wornOutfit`), else a bare form (the Wardrobe page's tab icon) for one their
  Default holds; a star (point down, lines from its centre to its inner corners, the upper left face filled) for an outfit the
  actor has for a situation (never Default alone). Each shows a tooltip while the mouse is
  over it: `Current Outfit` (and `Default Outfit` on a second line when their Default holds
  it), `Default Outfit` on the bare form, `For Town, Cold` (`OutfitBrowser::State::situations`,
  computed at Rebuild). The form and the star side by side when both apply.
- Rows meet with no gap (the list lays them out with no item spacing; each row holds the
  former gap, half above and half below, and 5px above and below its name), and every
  second row has the palette's Stripe colour under its fills (Vanilla and Nordic white at
  16/255, Modern black at 32/255), as Tailor 3.0's outfit list alternates its rows. Each
  name follows the outfit's icon (design.md §9), a square of the text height, 6px before
  it; the icon starts where a piece row's text starts (`Theme::TickTextShift`), or on a
  row with a drag grip 12px past its dots.
- The Filter dropdown is hidden behind a constant, its code kept: it is likely to return.
- Column labels over the list, as the piece list's (`Theme::HeaderRow`): an unnamed icon
  column centred over the rows' icons, then `NAME` over their names. A click on a label
  sorts by its column, a second one reverses the order, the arrow after the sorted label
  showing which way. The icon column shows its arrow alone; while Name sorts, the up arrow
  over the down one, 2px between their bases, faded until hovered, with the tooltip `Sort by
  icon`. Its click area is a row icon's height and reaches on either side to where `NAME`
  starts.
  Icon sorts by `OutfitIcons::kKeys`' order, then `kColors`' order, then the name (icons
  and colours not in the tables last); Name by the name, ASCII case ignored; descending
  reverses the whole order. Each page's list starts at Name ascending when the window opens.
- The count line under the list, as the piece list's: `12 outfit(s) in the wardrobe`, or
  `3 of 12 outfits` while the search narrows it. The list's height cap leaves room for it,
  and the list keeps the height of the whole library.
- The order and the situation mark text are computed when the list is refreshed, never per
  frame.

## 2. Inventory page (step 1)

- Each group heading of the piece list starts with an icon (`Theme::HeadingIcon`), an 18px
  square at 1080p in every skin, in its grey, centred on the text's line: the Inventory tab's
  backpack over `Inventory` and
  `#{Player}'s Inventory`, the dressed form of an outfit row's current-outfit mark over the
  three `Outfit (...)` headings, the bare form over the loaded outfit's.
- The count line ends in `Add items from Wardrobe...` (`Theme::LabelLink`, its plus mark
  first), which opens the Load page; greyed while the library is empty, showing
  `kNoOutfits` on hover. Not shown for the player while `Dress yourself from inventory only`
  is ticked (`Request::offersWardrobe`). The buttons are `Create / Update Outfit`, `Apply`, `Exit`.
- Tooltips on hover while enabled; a greyed button shows its reason instead:
  - Create / Update Outfit: `Save the ticked items as a new outfit, or into an existing one.`
  - Apply: `Dress #{Name} in the ticked items.`
  - Exit: none.
- `Restore (?)`, while Livery dresses the actor, sits at the right end of the button
  row (`Theme::LineHelpLink`), its text centred on the buttons' height, in the count
  line's font and colour, lit on hover; the `(?)` is inside its click area and hovering it
  shows the help. A click asks `Restore #{Name}'s original outfit?` / `This takes Livery's
  outfit off #{Name} and forgets every situational outfit chosen for them.` with `Restore` /
  `Cancel`.
- The `Player's Inventory` heading ends in a `(?)`; the tooltip shows while the mouse
  is over that mark alone: `"Apply" moves the ticked items from your inventory to #{Name}.`
  (or `"Apply" gives #{Name} a new copy of each ticked item; yours stay with you.`), then
  `Quest items are not listed.` or, with ProtectFavorites, `Quest items and your favorites
  are not listed.`
- The Apply popup's `No, apply as is (?)` help: `The ticked pieces become #{Name}'s Default
  as they are, without being added to the wardrobe.`, then, while a situation row of #{Name}
  holds an outfit, `While a situational outfit is worn, the new Default shows only when that
  situation ends.` A button
  with a `(?)` (the Apply popup's `No, apply as is`) draws it inside its frame.
- Vanilla and Nordic draw text fields and dropdowns underlined, not framed. A text field has
  a grey pen at its right end, white while the field or the pen is hovered or the field is
  being typed in; a click on the pen starts typing, so the click area is the field's. A
  dropdown's arrow is grey, white while hovered or open.
- A *field* is a label (or none) and the control or list it names; a list's count line under
  it belongs to the list's field. Beyond the item spacing, fields are 6px apart in Modern and
  8px in Vanilla and Nordic (`Theme::SectionGap`); a label sits 3px (Vanilla and Nordic: 5px)
  closer to its control than the item spacing; a count line under a list keeps 6px above it.
- A *button* sits in a page's or popup's button row (`Theme::Button`); a *link* is a clickable
  label in the label font beside a field or a line (`Restore Presets`, `Remove`, `Edit`).
  Vanilla draws its buttons as text alone, 2px larger than the body font, grey
  and white while hovered, with no fill, 2px clear on either side.

## 3. Tab column (step 2)

- A column of icon tabs at the window's left edge, a rule between it and the page:
  Inventory (a backpack), the Wardrobe page (a bare dress form: a padded torso on a stand,
  each shoulder one arc), the Situations page (the star, point down, of the outfit rows'
  situation mark; docs/situation-management-ui.md §3) and Appearance (a painter's palette: a
  round board with a bite out of its lower right, three dabs of paint across from it, smaller
  towards the upper right, each as far from the rim and from the next).
- Icons only, drawn as ImDrawList lines, so they scale with the resolution and take the
  skin's colours: the selected tab in the text colour on the selected row's fill (its corners
  rounded 2px), the others in
  LabelText's grey, lit on hover. Hover shows the tab's name as a tooltip.
- Each tab is centred between the window's left edge and the rule. Nordic's start 8px
  lower, clear of the corner knot.
- Shown on the Inventory, the Wardrobe page, the Situations page and Appearance pages only; the
  Load, Save, Select and Edit pages have none.
- Switching tabs keeps the Inventory page's ticks until the window closes. Leaving Manage
  Wardrobe or the Situations page with unsaved edits asks first (§4,
  docs/situation-management-ui.md §6).
- The rule: Modern, 1px in its rule colour (Tailor 3.0's sidebar divider). Vanilla, 2px in its
  rule grey. Nordic, NORDIC UI's vertical rope, the ornament left and right of the level-up
  menu's `Select an attribute to increase`, taken from its .swf with JPEXS and credited in
  `NORDIC-UI-CREDITS.txt`; without the texture, 1px in its rule colour.
- Nordic's corner knots frame the whole window, tab column included.

### Appearance page

- The SKSE Menu Framework page's Appearance section inside the window, so the look can be
  changed without that framework and seen at once. One column, as wide as the Inventory page.
- Heading `Appearance`, its summary line `Other settings are in Livery.ini or on the SKSE
  Menu Framework page.`
- `Window skin`: a dropdown of the palettes, as wide as its longest name, as the settings
  page's dropdown lists them. A choice is written to `[UI] Palette` at once and the window is
  drawn in it from the next frame (`Theme::Apply` runs every frame from `Settings::Skin` and
  `Palette`; every skin's fonts and textures are loaded at start). Restore original palette
  at the line's right end while the chosen shipped palette's file differs from what Livery
  ships: its popup `Restore Edge.ini as Livery ships it? Your changes to it will be lost.`,
  Restore / Cancel.
- `Play menu sounds`, a tick written to `[UI] MenuSounds` at once.
- Exit, which Escape also is. Nothing here is an unsaved edit, so leaving never asks.
- The settings page's dropdown and tick show the same state (`Settings::ListPalettes`,
  `ChoosePalette`, `RestoreChosenPalette`, `SetMenuSounds`); the log names the page a change
  came from.

## 4. The Wardrobe page (step 2)

- Title `Wardrobe` (the tab's name too), no summary line. Two columns as on the Load
  page: the outfit list (§1) on the left; on the right the selected outfit, editable:
  - icon and name on one line: the icon field (`Theme::IconField`), the outfit's icon with
    8px on either side, as tall as the name field, filled in the hover colour while hovered
    (`Outfit Icon` on hover); then the name, as the Save page's field.
  - A click on the icon field opens `Choose icon` (no title, as every popup): the icons
    whose PNG is loaded, in `OutfitIcons::kKeys`' order, 8 to a row, each a tile with 6px
    around it, filled in the hover colour while hovered and in the selected colour with a
    rim while chosen; under them the ten colours of `OutfitIcons::kColors`, one row as wide
    as the icons, a 1px rim while hovered and 2px while chosen; then `OK` / `Cancel`.
    While the mouse is over a colour, every icon shows in it; a click chooses an icon or a
    colour. OK makes the choice the edited icon, an edit as a rename is; Cancel or Escape
    leaves it. Nothing is written until Update. The Save page's Create tab opens the same
    popup for the new outfit's icon (docs/ui-redesign.md).
  - 4px of extra room, then its pieces under a count line ending in `Edit`
    (`Theme::LabelLink`). The count includes pieces from plugins that are not loaded, which
    stay in the outfit. The pieces are the Inventory page's tick rows (`Theme::TickRow`, with
    their icons, no column labels): the ticked ones are what Apply dresses the actor in, the
    unticked ones the items the outfit leaves off on the row Apply gives it (design.md §2).
    They start as that row leaves them, the row the actor wears the outfit for now, else the
    Default (all ticked when it does not have the outfit); the last ticked piece cannot be
    unticked. A tick is not an unsaved edit: selecting an outfit, or a change the window
    takes (`SetOutfits`), starts the ticks again from the rows; Update and Remove ignore
    them. The Load page's details and the Save page's piece columns keep the
    same room before their count line. Their read-only piece rows have the same icons
    (`Theme::TextRow`), and the outfit's name over the Load page's details, and over Manage
    Situations' outfit details, its icon as the outfit list draws it (`Theme::OutfitNameText`).
- With an empty library the page shows the empty list.
- Edits are kept for one outfit at a time. Selecting another outfit while the selected one
  has unsaved edits asks `You have unsaved changes for "#{Outfit}".` with the lines Update's
  popup lists, then `Discard changes` / `Keep editing`.
- The preview shows the selected outfit as edited, less its unticked pieces; with none
  selected, the Inventory page's ticks.
- Buttons `Update`, `Apply to #{Name}`, `Exit`, then `Remove` (step 3) at the row's right
  end, a link drawn as Restore is, without the `(?)` (`Theme::LineLink`).
  - Update: greyed until the outfit differs from the library (icon, trimmed name, pieces), and
    with an empty name (`kNeedName`). A popup (`Save changes?`) lists
    what changes (pieces, rename, `Its icon changes.`), as the Save page's Update tab does; the
    buttons are `Confirm` / `Cancel`. The window hands the edits to
    `Request::onManageUpdate`; no row changes. CLAUDE.md §2.1 applies: a name another outfit
    has (ASCII case ignored) is refused, and a refused or failed write changes nothing and
    shows the error. On success the window stays open: the list is rebuilt
    (`CaptureWindow::SetOutfits`), the edits start again from the library, the outfit stays
    selected (found by id), and the captured actor is redressed when they wear it (§6).
  - Apply to #{Name}: changes what the actor wears now. The outfit goes to one row in one
    `Assignments::Change`, its unticked pieces left off there (none unticked: all its items),
    the window staying open as after the Inventory page's Apply (§6); nothing moves from the
    player's inventory. The Save page's Create and Apply always gives every item. The row, from `Request::wornFor` (what
    `Request::wornOutfit` is worn for):
    - They wear another library outfit for a situation now: a popup (`Apply outfit?`) says
      so, `#{Name} is wearing their Town outfit, "B".` (random on: `one of their Town
      outfits`), and asks: random off `Make "A" their new Town outfit?` ("B" leaves the
      row's list), random on `Add "A" to them and wear it now?`, or with "A" on that list
      already `Wear "A" from them now?`; with library edits `Save your changes to "A", then
      make it ...?` and their lines. Buttons `Yes` (that row; random on, worn as the pick),
      `Apply as Default (?)` and `Cancel`. Apply as Default gives the Default the outfit,
      the situation's outfit staying; its help: `"A" becomes #{Name}'s Default instead, and
      their Town outfit stays. The new Default shows only when Town ends and no other
      situational outfit applies.`, then `It replaces the current Default, "C".` when it
      does, and `Your changes are saved first.` with library edits.
    - They wear it for a situation now: that row, dressed again.
    - Else (the Default, nothing of Livery's, or a situation removed since): the Default.
    - In the last two cases it asks first (`Apply outfit?`) only with library edits or a
    Default whose worn outfit it replaces, random off: `Apply "#{Outfit}" as #{Name}'s
    Default?` (`... to #{Name} for Town?`), with library edits `Save your changes to
    "#{Outfit}", then apply it ...?` and their lines, then `The current Default, "C", will be
    replaced.` when it is; buttons `Apply` (`Update and Apply` with library edits) /
    `Cancel`. With nothing to say it dresses at once. Its tooltip says which: `Dress #{Name}
    in this outfit as their Town outfit, or make it their Default.` / `... for Town.` /
    `... as their Default.` With library edits it goes through `onManageUpdate` with
    `apply` and the row: the library write first (CLAUDE.md §2.1), a refused or failed one
    assigns nothing and shows the error; else through `Request::onManageApply` with the
    row. Greyed with no outfit selected, with no edited piece (`kNoLoadedPiece`), with
    library edits and an empty name (`kNeedName`), and with no library edits while the actor
    wears the outfit now, every piece Livery put on still worn (`Request::piecesNotWorn` 0)
    and nothing else worn (`Request::extraWorn` 0), for the Default or a situation not
    removed, with the ticks that row's (`kWornAlready`), as Apply would change
    nothing.
  - Exit closes the window. Escape does what Exit does (it first clears a search, as on the
    Load page).
- Exit, Escape or another tab with unsaved edits shows `You have unsaved changes for
  "#{Outfit}".` with the lines Update's popup lists, then `Save Changes` /
  `Discard Changes` / `Back` (docs/situation-management-ui.md §6): Save Changes opens
  Update's popup, and its Confirm saves, then goes once the write took (a refused one stays;
  Cancel stays); Discard Changes starts the edits again from the library and goes; Back stays.

### Edit page

- Reached from `Edit`: the Inventory page's piece list (headings, columns, sorting) with the
  outfit's edited pieces listed and ticked exactly, those nobody carries under their own
  heading, as after Load. Its ticks are its own; the Inventory page's stay as they were.
- Title `Edit "#{Outfit}"`, no summary line, no tab column. It has the window's Live Preview
  tick; the preview shows this page's ticks.
- The count line ends in `Add items from Wardrobe...`, as on the Inventory page (§2): the
  Load page opens, and Load adds the selected outfit's pieces to this page's ticks instead
  of replacing them: a ticked piece sharing a body slot with one of them is unticked, as a
  tick would untick it. Its pieces nobody carries stay listed, and their heading names every
  outfit they come from (`"A", "B"`). While the Load page is open the
  preview shows this page's ticks with the selected outfit added; Load and Back return to
  this page, nothing written.
- `Use Ticked Items` (needs a tick, `kNeedPiece`): the ticks become the outfit's edited
  pieces and the page returns to the Wardrobe page, its unticked pieces among them staying
  unticked (all ticked again when they were all); nothing is written. `Back` and Escape
  return with the edited pieces as they were.

## 5. Remove (step 3)

- `Remove` on the Wardrobe page, enabled with an outfit selected (greyed with `kNeedOutfit` on
  hover without one). A popup names the outfit and
  lists who uses it among Livery's assignments in this save: each actor (their FormID when
  not in memory) with the rows that hold it (`Lydia: Default, Town`), then `Remove` /
  `Cancel`.
- Order: the outfit's loaded pieces are read, then `Library::Remove` writes the file; a
  refused or failed write changes no assignment. Then `Assignments::ForgetOutfit` takes the
  outfit off every row (docs/situation-management-ui.md §1): a `chosen` that was it becomes
  0, so an outfit hidden behind it is not worn, and a Default this leaves wearing nothing
  gets those pieces as its pieces. One HUD message sums it up.
- An actor wearing it for a situation is redressed for what applies now (§6), one not in
  memory at their next situation change; a random row with others on its list picks again
  from them. A Default worn from it stays on.
- Afterwards the list is rebuilt without it and nothing is selected; the preview shows the
  Inventory page's ticks, or, when the captured actor was redressed, what they wear now, the
  Inventory page following them (§6). The Inventory page's loaded
  outfit and the Save page's chosen outfit are cleared when they were it.
- Other saves change nothing at load: their rows keep the removed outfit's id, which counts
  as not on the row and never shows (docs/situation-management-ui.md §1), and restoring
  outfits.json brings it back.

## 6. Redressing while the window is open (steps 2 and 3)

Assignments run only with no preview on, since the preview restores what it found at
`OutfitPreview::Begin`. A Wardrobe page change that redresses:

- the captured actor, at once, Live Preview on or off. With it on, one SKSE task runs
  `OutfitPreview::End`, the assignment change, then `OutfitPreview::Begin` with the outfit
  The Wardrobe page has selected (Update), or with what they wear now (Remove), so the new
  state is what the preview later restores. The camera and the movement hold stay. When
  Begin refuses, the Live Preview tick greys with the reason, as at Open. With it off, the
  assignment runs in the game the window froze, and the NPC's model is rebuilt at once
  (`Dressing::RefreshModel`; docs/design.md §6). Then the Inventory page follows what they wear, as Open would list
  it now (`CaptureWindow::OutfitList::actorPieces`): each piece's heading, the worn pieces
  ticked (the user's ticks replaced), the heading of Livery's outfit named after the one now
  on them, the loaded outfit let go. Armor the window did not list at Open cannot be listed,
  since its indices are fixed: it shows on the preview but is not ticked.
- any other actor, taken out of a removed outfit or situation: queued and run in `onClose`,
  after the preview ended.
- A load closing the window (`kPreLoadGame`) drops the queue.

What redresses the captured actor: Update of the outfit they wear, for any row,
`Assignments::Refresh`, which reads it from the library again. Pieces are added as Assign
adds them (an NPC gets a copy of a piece they lack); nothing moves from the player's
inventory. Not worn now: nothing, since every row reads its outfit from the library each
time it goes on. Remove: §5.

An Apply (the Inventory page's, Apply to #{Name}, Update and Apply) dresses the captured
actor at once, Live Preview on or off, as above, and the window stays on its page; with
Live Preview on the preview begins again on what they wear now (the Inventory page) or the
outfit the Wardrobe page has selected. The Inventory page follows what they wear
(`actorPieces`), and `Restore` shows while Livery dresses them
(`OutfitList::dressed`). The Save page's Create and Apply and Restore close the window
first.

## 7. Code

- `src/CaptureWindow.cpp` (about 2400 lines) splits at the start of step 2, in a move-only
  commit: the outfit browser, the outfit lists and the Load, the Wardrobe page and Edit
  pages go to `src/OutfitPages.cpp`; the state and shared helpers to a header private to
  the two files. `CMakeLists.txt` gains the file (CLAUDE.md §1, build system).
- `CaptureWindow`: pages for the Wardrobe page and Edit; `SetOutfits` (the rebuilt outfits
  and rows, the window's indices into them found again by id); a call that greys
  Live Preview with a reason; `Request::onManageUpdate` and, in step 3,
  `onManageRemove` and each outfit's users for Remove's popup; `onManageApply` with one row
  (`Request::wornFor` tells the row of the outfit worn); Apply's popup body
  (`DrawApplyPopupBody`, `ApplySubject`), which the Save page's Create and Apply shares.
- `OutfitBrowser`: sort keys Name, Situation (`SetSort`); per outfit its heading,
  rows and situation mark text; the folded headings. Unit tests cover the keys, the
  headings, the folding and the marks.
- `Library`: `Remove` (step 3), written through the temporary file as Create and Update are.
- `Assignments`: `ForgetOutfit` (step 3), returning the actors wearing it.
- `Theme`: the tab column, its icons and rule (Nordic's rope texture), the outfit row with
  its marks (`OutfitMarks`), the folding headings of a sorted outfit
  list (`FoldHeading`), the rows' fills and NORDIC UI's line, the dropdown rows
  (`DropdownRow`), a label's `(?)` (`LabelHelpMark`), a list heading's (`ListHeadingHelp`),
  a link with its `(?)` (`LabelHelpLink`), a button with one (`HelpButton`), the underlined
  fields with the pen, the icons (bare and dressed form, star, palette), `AfterFieldGap`.
- `Plugin.cpp`: the Situation group for `Request::categories` and each outfit's rows, the
  The Wardrobe page handlers, the redress queue (§6).

## 8. Checks

- Step 1: each sort's headings, folded and unfolded; Load from its button; the three skins.
- Step 2: switching tabs keeps the ticks and the edits; Update's popup, HUD message and
  rebuilt list; a taken name refused; Edit, Use Ticked Items and Back; the three warnings;
  Update of the outfit the captured actor wears, at once with Live Preview on and off (the
  frozen game showing the new outfit, no empty torso); the three skins.
- Step 3: Remove of an outfit in use lists its users; the captured actor wearing it for a
  situation changes at once, Live Preview on or off; a default from it keeps its
  pieces; the assignments survive save and load.
- Situations and Apply: ticking a free situation and Default, then one that wears another
  outfit (`"B" replaces "A"`, Confirm, Cancel) and saving with Update, Live Preview on (at once) and off
  (at close); Apply to #{Name} with nothing to say, with edits (Update and Apply; a taken
  name refused, nothing assigned), for ticked rows and as a replaced Default; the row marks
  and their tooltips; Restore's popup; the heading's `(?)`; Exit with and without edits;
  leaving for another tab with edits, Save Changes, Discard Changes and Back.
