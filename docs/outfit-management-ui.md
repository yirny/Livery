# Outfit management UI

Written 2026-10-20 for the AI that implements it, from the user's draft and the decisions
made on it; current spec only. Three steps, each built, checked in game and committed
before the next, each with CLAUDE.md §1 approval first: (1) the outfit list and the
Inventory page's buttons, (2) the tab column and the Manage Outfits page with Update,
(3) Remove. Docs follow the code in the step that makes them stale: design.md, README, and
CLAUDE.md §2.1 (step 2 adds the Manage Outfits Update path, step 3 the window's delete).

The Inventory page is today's Capture page (`Page::kCapture`), titled `#{Name}'s Inventory`.
Docs call it the Inventory page; code identifiers stay.

## 1. Outfit list (step 1)

The outfit list shared by the Load page, the Save page's outfit selection (the Select page)
and the Manage Outfits page. Each page keeps its own search and sort.

- Wording: `Armor Class` everywhere (the Save page's field, the details column, the sort
  option, the filter group) except the piece list's column header, which stays `Class`.
- Categories: Light, Heavy, Clothing. Jewelry leaves the categories; the piece list's Class
  column keeps it. `Library::Load` drops a stored `Jewelry` in memory; the file loses it at
  the next write. The suggested class skips Jewelry pieces as it skips Other; with none
  left, Clothing.
- Three category groups, in this order:
  - Armor Class: the classes above, which the Save page and Manage Outfits edit.
  - Custom Category: every stored category that is not an Armor Class, across the library,
    by name. Older files' situation categories (Adventuring, Town, ...) are custom
    categories now. Read only until the custom category UI (design.md §8).
  - Situation: the captured actor's "wears it for" rows that hold the outfit (Default and
    the situations), from `Request::assigned`. Never stored in the library.
- Each row: the name, then under it, in the small font and grey, its tags in this order:
  Female or Male (Unisex has none), Armor Class, Custom Category, Situation, wrapping onto
  more lines when they do not fit. Tailor draws each tag as a framed chip, as Tailor 3.0's
  outfit list does; Vanilla and Nordic as plain text, comma separated.
- A `Show Categories` tick under the list, on each time the window opens; off, rows show the
  name only.
- The Filter dropdown is hidden behind a constant, its code kept: it is likely to return.
- `Sort by`: Name, Gender, Armor Class, Custom Category (offered only while some outfit has
  one), Situation. Every key but Name puts headings in the list: the outfit's categories in
  that group joined with `, ` (`Town, Swimming`), in the group's order (Light, Heavy,
  Clothing; Default, then `Situations::kChoosable`; custom ones by name). Gender's headings
  are Female, Male, Unisex, in that order. Outfits with none of the group's categories come
  last under `No Armor Class`, `No Custom Category` or `No Situation`. Within a heading, by
  name.
- Headings and tag text are computed when the list is refreshed, never per frame.

## 2. Inventory page (step 1)

- `Load Outfit...` leaves the count line; the buttons are `Load`, `Save`, `Apply`, `Cancel`.
  Load is greyed while the library is empty (`kNoOutfits`) and opens the Load page.
- Tooltips on hover while enabled; a greyed button shows its reason instead:
  - Load: `Add a saved outfit's pieces to the list.`
  - Save: `Save the ticked pieces as a new outfit, or into an existing one.`
  - Apply: `Dress #{Name} in the ticked pieces.`
  - Cancel: none.

## 3. Tab column (step 2)

- A column of icon tabs at the window's left edge, a rule between it and the page:
  Inventory (a backpack) and Manage Outfits (a dress form: a padded torso on a stand).
  Manage Situations (a simple snowflake) comes with the Situation Management window.
- Icons only, drawn as ImDrawList lines, so they scale with the resolution and take the
  skin's colours: the selected tab in the text colour on a faint fill, the others in
  LabelText's grey, lit on hover. Hover shows the tab's name as a tooltip.
- Shown on the Inventory and Manage Outfits pages only; the Load, Save, Select and Edit
  pages have none.
- Switching tabs keeps everything: the Inventory page's ticks and Manage Outfits' edits stay
  until the window closes.
- The rule: Tailor, 1px in its border colour (Tailor's sidebar divider). Vanilla, 2px in its
  rule grey. Nordic, NORDIC UI's vertical rope, the ornament left and right of the level-up
  menu's `Select an attribute to increase`, taken from its .swf with JPEXS and credited in
  `NORDIC-UI-CREDITS.txt`; without the texture, 1px in its rule colour.
- Nordic's corner knots frame the whole window, tab column included.

## 4. Manage Outfits page (step 2)

- Title `Manage Outfits`, no summary line. Two columns as on the Load page: the outfit list
  (§1) on the left; on the right the selected outfit, editable:
  - name, Armor Class and gender, as the Save page's fields. No "wears it for": assignments
    belong to the Save page and, later, the Situation Management window.
  - its pieces, read only, under a count line ending in `Edit` (`Theme::LabelLink`). The
    count includes pieces from plugins that are not loaded, which stay in the outfit.
- With an empty library the page shows the empty list.
- Edits are kept for one outfit at a time. Selecting another outfit while the selected one
  has unsaved edits asks `Discard changes` / `Keep editing`.
- The preview shows the selected outfit as edited; with none selected, the Inventory page's
  ticks.
- Buttons `Update`, `Remove` (step 3), `Cancel`.
  - Update: greyed until the outfit differs from the library (trimmed name, Armor Class,
    gender, pieces), and with an empty name (`kNeedName`). A popup lists what changes
    (pieces, Armor Class, rename, gender), as the Save page's Update tab does, then the
    window hands over to `Request::onManageUpdate`. CLAUDE.md §2.1 applies: a name another
    outfit has (ASCII case ignored) is refused, and a refused or failed write changes
    nothing and shows the error. On success the window stays open: the list is rebuilt
    (`CaptureWindow::SetOutfits`), the edits are cleared, the outfit stays selected (found
    by id), and the captured actor is redressed when they wear it (§6).
  - Cancel and Escape close the window (Escape first clears a search, as on the Load page).
    With unsaved edits they first ask `Discard and close` / `Stay`.
- Leaving for the Inventory tab with unsaved edits shows
  `You have unsaved changes for "#{Outfit}".` with `OK`; the edits stay. Closing the window
  from the Inventory page (Save, Apply, Restore, Cancel) drops them without asking.

### Edit page

- Reached from `Edit`: the Inventory page's piece list (headings, columns, sorting) with the
  outfit's edited pieces listed and ticked exactly, those nobody carries under their own
  heading, as after Load. Its ticks are its own; the Inventory page's stay as they were.
- Title `Edit "#{Outfit}"`, no summary line, no tab column. It has the window's Live Preview
  tick; the preview shows this page's ticks.
- `Use Ticked Pieces` (needs a tick, `kNeedPiece`): the ticks become the outfit's edited
  pieces and the page returns to Manage Outfits; nothing is written. `Back` and Escape
  return with the edited pieces as they were.

## 5. Remove (step 3)

- `Remove` on Manage Outfits, enabled with an outfit selected. A popup names the outfit and
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
`OutfitPreview::Begin`. A Manage Outfits change that redresses:

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

## 7. Code

- `src/CaptureWindow.cpp` (about 2400 lines) splits at the start of step 2, in a move-only
  commit: the outfit browser, the outfit lists and the Load, Select, Manage Outfits and Edit
  pages go to `src/OutfitPages.cpp`; the state and shared helpers to a header private to
  the two files. `CMakeLists.txt` gains the file (CLAUDE.md §1, build system).
- `CaptureWindow`: pages for Manage Outfits and Edit; `SetOutfits` (the rebuilt outfits,
  categories and rows, the window's indices into them found again by id); a call that greys
  Live Preview with a reason; `Request::onManageUpdate` and, in step 3, `onManageRemove` and
  each outfit's users for Remove's popup.
- `OutfitBrowser`: sort keys Name, Gender, Armor Class, Custom Category, Situation; per
  outfit its heading and tag text. Unit tests cover the keys, the headings and a library
  without custom categories.
- `Library`: Jewelry out of `Categories()` and dropped at Load; `Remove` (step 3), written
  through the temporary file as Create and Update are.
- `Assignments`: `ForgetOutfit` (step 3), returning the actors wearing it.
- `Theme`: the tab column, its icons and rule (Nordic's rope texture), the outfit row with
  tags (Tailor's chips), the headings of a sorted outfit list.
- `Plugin.cpp`: the three category groups for `Request::categories` and each outfit, the
  Manage Outfits handlers, the redress queue (§6).

## 8. Checks

- Step 1: each sort's headings; Show Categories off; a library without custom categories
  (no Custom Category sort); Load from its button; an old outfit with Jewelry loads without
  it; the three skins.
- Step 2: switching tabs keeps the ticks and the edits; Update's popup, HUD message and
  rebuilt list; a taken name refused; Edit, Use Ticked Pieces and Back; the three warnings;
  Update of the outfit the captured actor wears, with Live Preview on (at once) and off (at
  close); the three skins.
- Step 3: Remove of an outfit in use lists its users; the captured actor wearing it for a
  situation changes at once (Live Preview on) or at close; a default from it keeps its
  pieces; the assignments survive save and load.

## 9. Later

- A UI for custom categories (design.md §8).
- The Manage Situations tab, with the Situation Management window.
