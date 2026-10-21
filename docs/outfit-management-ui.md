# Outfit management UI

Written 2026-10-20 for the AI that implements it, from the user's draft and the decisions
made on it; current spec only. Three steps, each built, checked in game and committed
before the next, each with CLAUDE.md §1 approval first: (1) the outfit list and the
Inventory page's buttons, (2) the tab column and the Manage Wardrobe page with Update,
(3) Remove. Docs follow the code in the step that makes them stale: design.md, README, and
CLAUDE.md §2.1 (step 2 adds the Manage Wardrobe Update path, step 3 the window's delete).

The Inventory page is today's Capture page (`Page::kCapture`), titled `#{Name}'s Inventory`.
Docs call it the Inventory page; code identifiers stay.

## 1. Outfit list (step 1)

The outfit list shared by the Load page, the Save page's outfit selection (the Select page)
and the Manage Wardrobe page. Each page keeps its own search and sort.

- Wording: `Category` everywhere (the Save page's field, the details column, the sort
  option, the filter group, the piece list's column header); the window calls the library
  the wardrobe (`12 outfit(s) in the wardrobe`, `Not in Wardrobe`).
- Categories: Light, Heavy, Clothing. Jewelry leaves the categories; the piece list's
  Category column keeps it. `Library::Load` drops a stored `Jewelry` in memory; the file
  loses it at the next write. The suggested category skips Jewelry pieces as it skips Other;
  with none left, Clothing. Other stored categories (older files' Adventuring, Town, ...)
  stay in the file, unshown, until categories can be added (design.md §8).
- Two category groups, in this order:
  - Category: the categories above, which the Save page and Manage Wardrobe edit.
  - Situation: the captured actor's "wears it for" rows that hold the outfit (Default and
    the situations), from `Request::assigned`. Never stored in the library.
- Each row: the name, then under it, in the small font and grey, its tags in this order:
  Female or Male (Unisex has none), Category, Situation, wrapping onto more lines when they
  do not fit. Tailor draws each tag as a framed chip, as Tailor 3.0's outfit list does;
  Vanilla and Nordic as plain text, comma separated.
- Hover, in every list and in the dropdowns' rows: Tailor fills a hovered row in its muted
  brown; Vanilla lights it with a soft white ellipse fading out to the row's edges, as SkyUI
  lights its MCM title bar (a texture made at load, `Theme::LoadTextures`); Nordic draws
  NORDIC UI's list selection line under it (skyui/inventorylists.swf, `ItemsListEntry`'s
  `selectIndicator`, shape 1: a thin wedge pointed at its left end, #e5e5e5 fading from 0.4
  to nothing), from the shape's points in code, stretched across the row, as thick as the
  shape is in a 28-unit entry of the frame height.
- Selected (a single-choice row, a ticked piece, a ticked dropdown row): Tailor fills it
  faintly amber; Vanilla lays the piece list's white bar fading in from the left over it;
  Nordic shows no fill. The mark: NORDIC UI's MCM page arrow on single-choice and dropdown
  rows in Vanilla and Nordic (Nordic's 4px larger, its window padding 32px), the equipped
  mark on piece rows; Nordic puts the arrow on a ticked dropdown row too, at its start.
- A multi-choice dropdown row: Vanilla and Nordic draw the game's checkbox at the row's start
  (after Nordic's arrow), then the label; Tailor a check mark at its right end.
- Marks at the right end of each row's name line, in LabelText's grey, the name cut short
  of them: a dressed form (a gown on the stand) for the outfit the actor wears now
  (`Request::wornOutfit`), a star (a pentagram with its inner pentagon) for an outfit the
  actor has for a situation (never Default alone). Each shows a tooltip while the mouse is
  over it: `Current outfit`, `For Town, Warm` (`OutfitBrowser::State::situations`, computed
  at Rebuild). Both side by side when both apply; drawn with or without Show Categories.
- `Sort by`'s text is right-aligned, ending 6px short of its arrow.
- A `Show Categories` tick under the list, on each time the window opens; off, rows show the
  name only.
- The Filter dropdown is hidden behind a constant, its code kept: it is likely to return.
- `Sort by`: Name, Gender, Category, Situation. Every key but Name puts headings in the
  list: the outfit's categories in that group joined with `, ` (`Town, Swimming`), in the
  group's order (Light, Heavy, Clothing; Default, then `Situations::kChoosable`). Gender's
  headings are Female, Male, Unisex, in that order. Outfits with none of the group's
  categories come last under `No Category` or `No Situation`. Within a heading, by name.
- A heading has a chevron on its left, pointing down while open and right while folded, and
  a rule in the list's rule colour from 4px after its text to 4px short of the list's right
  end. A click on the heading folds its outfits away or back (`OutfitBrowser::State::folded`,
  by heading text). Every heading is open when the window opens and when the sort changes;
  the list keeps the height of the whole library.
- Headings and tag text are computed when the list is refreshed, never per frame.

## 2. Inventory page (step 1)

- `Load Outfit...` leaves the count line; the buttons are `Load`, `Save`, `Apply`, `Cancel`.
  Load is greyed while the library is empty (`kNoOutfits`) and opens the Load page.
- Tooltips on hover while enabled; a greyed button shows its reason instead:
  - Load: `Add a saved outfit's pieces to the list.`
  - Save: `Save the ticked pieces as a new outfit, or into an existing one.`
  - Apply: `Dress #{Name} in the ticked pieces.`
  - Cancel: none.
- `Restore Original Outfit (?)`, while Livery dresses the actor, sits at the right end of
  the count line in its font and colour, lit on hover; the `(?)` is inside its click area
  and hovering it shows the help. A click asks `Restore #{Name}'s original outfit?` / `This
  takes Livery's outfit off #{Name} and forgets every situation outfit chosen for them.` with
  `Restore` / `Cancel`.
- The `From Player's Inventory` heading ends in a `(?)`; the tooltip shows while the mouse
  is over that mark alone: `"Apply" moves the ticked pieces from your inventory to #{Name}.`
  (or `"Apply" gives #{Name} a new copy of each ticked piece; yours stay with you.`), then
  `Quest items are not listed.` or, with ProtectFavorites, `Quest items and your favorites
  are not listed.`
- The Apply popup's `No, just apply (?)` help: `The ticked pieces become #{Name}'s Default
  without being added to the wardrobe. While a situation outfit is worn, the new Default
  shows only when that situation ends.` A button with a `(?)` (the Apply popup's `No, just apply`)
  draws it inside its frame.
- Vanilla and Nordic draw text fields and dropdowns underlined, not framed. A text field has
  a grey pen at its right end, white while the field or the pen is hovered or the field is
  being typed in; a click on the pen starts typing, so the click area is the field's. A
  dropdown's arrow is grey, white while hovered or open.

## 3. Tab column (step 2)

- A column of icon tabs at the window's left edge, a rule between it and the page:
  Inventory (a backpack) and Manage Wardrobe (a bare dress form: a padded torso on a stand,
  each shoulder one arc).
  Manage Situations (a simple snowflake) comes with the Situation Management window.
- Icons only, drawn as ImDrawList lines, so they scale with the resolution and take the
  skin's colours: the selected tab in the text colour on a faint fill, the others in
  LabelText's grey, lit on hover. Hover shows the tab's name as a tooltip.
- Each tab is centred between the window's left edge and the rule. Nordic's start 8px
  lower, clear of the corner knot.
- Shown on the Inventory and Manage Wardrobe pages only; the Load, Save, Select and Edit
  pages have none.
- Switching tabs keeps everything: the Inventory page's ticks and Manage Wardrobe' edits stay
  until the window closes.
- The rule: Tailor, 1px in its border colour (Tailor's sidebar divider). Vanilla, 2px in its
  rule grey. Nordic, NORDIC UI's vertical rope, the ornament left and right of the level-up
  menu's `Select an attribute to increase`, taken from its .swf with JPEXS and credited in
  `NORDIC-UI-CREDITS.txt`; without the texture, 1px in its rule colour.
- Nordic's corner knots frame the whole window, tab column included.

## 4. Manage Wardrobe page (step 2)

- Title `Manage Wardrobe` (the tab's name too), no summary line. Two columns as on the Load
  page: the outfit list (§1) on the left; on the right the selected outfit, editable:
  - name and gender, as the Save page's fields; then Category as a multi-choice dropdown of
    the window's categories, its frame naming the ticked ones or `None`.
  - `Situations for #{Name}` with a `(?)` (the choices are this actor's own, saved with
    Update or Apply; other characters keep theirs), over a multi-choice dropdown of the
    rows, Default first, the captured actor wears the outfit for, its frame naming them
    (`Default, Town`) or `None`. The ticks are part of the edits (`ManageForm::wearsFor`),
    started from the actor's rows.
  - 4px of extra room, then its pieces, read only, under a count line ending in `Edit`
    (`Theme::LabelLink`). The count includes pieces from plugins that are not loaded, which
    stay in the outfit. The Load and Select pages' details keep the same room before their
    count line.
- With an empty library the page shows the empty list.
- Edits are kept for one outfit at a time. Selecting another outfit while the selected one
  has unsaved edits asks `Discard changes` / `Keep editing`.
- The preview shows the selected outfit as edited; with none selected, the Inventory page's
  ticks.
- Buttons `Update`, `Remove` (step 3), `Apply to #{Name}`, `Exit`.
  - Update: greyed until the outfit differs from the library (trimmed name, Category,
    gender, pieces) or its rows differ from the actor's, and with an empty name
    (`kNeedName`). A popup (`Save changes?`) lists what changes (pieces, Category, rename,
    gender, then `Town: added` / `Town: removed`), as the Save page's Update tab does; a
    ticked row another outfit holds reads `Town: "A" → "B"` and the buttons are `Replace`
    / `Keep` (saves without those rows; `Keep "A"`, `Keep All`) / `Cancel`, else `Confirm`
    / `Cancel` (random picks, docs/ui-redesign.md §7, will skip the question). The window
    hands the edits and rows to `Request::onManageUpdate`. CLAUDE.md §2.1 applies: a name
    another outfit has (ASCII case ignored) is refused, and a refused or failed write changes
    nothing, assigns nothing and shows the error. On success the rows are changed in one
    `Assignments::Change` (§6), the window stays open: the list is rebuilt
    (`CaptureWindow::SetOutfits`), the edits start again from the library, the outfit stays
    selected (found by id), the Save page's Update tab ticks follow the rows changed for its
    outfit, and the captured actor is redressed when they wear it (§6).
  - Apply to #{Name}: dresses the actor in the outfit for the ticked rows, as the Save page
    gives it (`Assignments::Change`), or makes it their Default with none ticked
    (`Assignments::Assign(pieces, id)`), then the window closes, as the Inventory page's
    Apply does; nothing moves from the player's inventory. Its tooltip says which: `Dress
    #{Name} in this outfit for Town, Warm.` / `... as their Default.` It asks first
    (`Apply outfit?`) when there is anything to say: library edits (`Save your changes to
    "#{Outfit}", then apply it to #{Name} for Town?` and their lines, the button `Update and
    Apply`), row changes (`Town: added`, `Town: "A" → "B"`, which Apply takes over) or,
    with no row ticked, a Default it replaces (`#{Name} already has a Default outfit, "A".
    It will be replaced.`); else `Apply "#{Outfit}" to #{Name} for Town?` with `Apply` /
    `Cancel`; with nothing to say it dresses at once. With library edits it goes through
    `onManageUpdate` with `apply`: the library write first (CLAUDE.md §2.1), a refused or
    failed one assigns nothing and shows the error; else through `Request::onManageApply`
    with the rows. Greyed with no outfit selected, with no edited piece (`kNoLoadedPiece`),
    and with library edits and an empty name (`kNeedName`).
  - Exit closes the window; with unsaved edits it first asks `Discard changes and exit` /
    `Back`. Escape does what Exit does (it first clears a search, as on the Load page).
- Leaving for the Inventory tab with unsaved edits shows
  `You have unsaved changes for "#{Outfit}".` with `OK` / `Back`: OK switches and the edits
  stay, Back stays on Manage Wardrobe. Closing the window from the Inventory page (Save,
  Apply, Restore, Cancel) drops them without asking.

### Edit page

- Reached from `Edit`: the Inventory page's piece list (headings, columns, sorting) with the
  outfit's edited pieces listed and ticked exactly, those nobody carries under their own
  heading, as after Load. Its ticks are its own; the Inventory page's stay as they were.
- Title `Edit "#{Outfit}"`, no summary line, no tab column. It has the window's Live Preview
  tick; the preview shows this page's ticks.
- `Use Ticked Pieces` (needs a tick, `kNeedPiece`): the ticks become the outfit's edited
  pieces and the page returns to Manage Wardrobe; nothing is written. `Back` and Escape
  return with the edited pieces as they were.

## 5. Remove (step 3)

- `Remove` on Manage Wardrobe, enabled with an outfit selected. A popup names the outfit and
  lists who uses it among Livery's assignments in this save: each actor (their FormID when
  not in memory) with their rows (`Lydia: Default, Town`), then `Remove` / `Cancel`.
- Order: `Library::Remove` writes the file first; a refused or failed write changes no
  assignment. Then `Assignments::ForgetOutfit` clears every situation row holding the
  outfit; a default from it keeps its pieces with outfit id 0. One HUD message sums it up.
- An actor wearing it for a situation is redressed for what applies now (§6), one not in
  memory at their next situation change. A default worn from it stays on.
- Afterwards the list is rebuilt without it and nothing is selected; the preview shows the
  Inventory page's ticks. The Inventory page's loaded outfit and the Save page's chosen
  outfit are cleared when they were it.
- Other saves change nothing at load: a situation outfit the library no longer has puts
  nothing on and falls back to the default (design.md §7), and restoring outfits.json
  brings it back.
- Once situation lists with random picks exist (docs/ui-redesign.md §7), a list loses the
  outfit, and a random pick of it picks again from the rest.

## 6. Redressing while the window is open (steps 2 and 3)

Assignments run only with no preview on, since the preview restores what it found at
`OutfitPreview::Begin`. A Manage Wardrobe change that redresses:

- the captured actor, Live Preview on: one SKSE task runs `OutfitPreview::End`, the
  assignment change, then `OutfitPreview::Begin` with the pieces the window shows, so the
  new state is what the preview later restores. The camera and the movement hold stay.
  When Begin refuses, the Live Preview tick greys with the reason, as at Open.
- the captured actor, Live Preview off: queued and run in `onClose`, after the preview ended.
- any other actor: queued and run in `onClose`.
- A load closing the window (`kPreLoadGame`) drops the queue.

What redresses the captured actor: Update of the outfit they wear as their default gives
the default the new pieces (`Assignments::Assign(pieces, id)`); worn for a situation,
`Assignments::Refresh`. Pieces are added as Assign adds them (an NPC gets a copy of a piece
they lack); nothing moves from the player's inventory. Not worn now: nothing, since a
situation outfit is read from the library each time it goes on and a default from it keeps
its pieces until assigned again. Remove: §5.

Rows Update changed (`SaveRequest::assign` / `remove`) are made and dressed for in one
`Assignments::Change`, through the same path (`PendingRedress::change`: at once with Live
Preview on, in `onClose` with it off). Apply to #{Name} closes the window first, so it
assigns with the preview ended.

## 7. Code

- `src/CaptureWindow.cpp` (about 2400 lines) splits at the start of step 2, in a move-only
  commit: the outfit browser, the outfit lists and the Load, Select, Manage Wardrobe and Edit
  pages go to `src/OutfitPages.cpp`; the state and shared helpers to a header private to
  the two files. `CMakeLists.txt` gains the file (CLAUDE.md §1, build system).
- `CaptureWindow`: pages for Manage Wardrobe and Edit; `SetOutfits` (the rebuilt outfits,
  categories and rows, the window's indices into them found again by id); a call that greys
  Live Preview with a reason; `Request::onManageUpdate` (with the rows) and, in step 3,
  `onManageRemove` and each outfit's users for Remove's popup; `onManageApply` with the rows;
  the Save page's and Manage Wardrobe's row helpers shared (`TakenRows`, `AssignedRows`,
  `RemovedRows`, `DrawRowChangeLines`).
- `OutfitBrowser`: sort keys Name, Gender, Category, Situation (`SetSort`); per outfit its
  heading, tag text and situation mark text; the folded headings. Unit tests cover the
  keys, the headings, the stored categories the window does not offer, the folding and the
  marks.
- `Library`: Jewelry out of `Categories()` and dropped at Load; `Remove` (step 3), written
  through the temporary file as Create and Update are.
- `Assignments`: `ForgetOutfit` (step 3), returning the actors wearing it.
- `Theme`: the tab column, its icons and rule (Nordic's rope texture), the outfit row with
  tags (Tailor's chips) and marks (`OutfitMarks`), the folding headings of a sorted outfit
  list (`FoldHeading`), the rows' fills and NORDIC UI's line, the dropdown rows
  (`DropdownRow`), a label's `(?)` (`LabelHelpMark`), a list heading's (`ListHeadingHelp`),
  a link with its `(?)` (`LabelHelpLink`), a button with one (`HelpButton`), the underlined
  fields with the pen, the icons (bare and dressed form, star), `AfterFieldGap`.
- `Plugin.cpp`: the two category groups for `Request::categories` and each outfit, the
  Manage Wardrobe handlers, the redress queue (§6).

## 8. Checks

- Step 1: each sort's headings, folded and unfolded; Show Categories off; an old file's
  situation categories stay unshown; Load from its button; an old outfit with Jewelry loads
  without it; the three skins.
- Step 2: switching tabs keeps the ticks and the edits; Update's popup, HUD message and
  rebuilt list; a taken name refused; Edit, Use Ticked Pieces and Back; the three warnings;
  Update of the outfit the captured actor wears, with Live Preview on (at once) and off (at
  close); the three skins.
- Step 3: Remove of an outfit in use lists its users; the captured actor wearing it for a
  situation changes at once (Live Preview on) or at close; a default from it keeps its
  pieces; the assignments survive save and load.
- Situations and Apply: ticking a free situation and Default, then one another outfit
  holds (Replace, Keep, Cancel) and saving with Update, Live Preview on (at once) and off
  (at close); Apply to #{Name} with nothing to say, with edits (Update and Apply; a taken
  name refused, nothing assigned), for ticked rows and as a replaced Default; the row marks
  and their tooltips; Restore's popup; the heading's `(?)`; Exit with and without edits;
  leaving for the Inventory tab with edits, OK and Back.

## 9. Later

- Adding categories beyond Light, Heavy and Clothing (design.md §8).
- The Manage Situations tab, with the Situation Management window.
