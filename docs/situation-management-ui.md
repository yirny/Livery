# Situation management

Written 2026-10-21 for the AI that implements it, from the user's draft and the decisions
made on it; current spec only. Three steps, each in its own session, with CLAUDE.md §1
approval first, built, checked in game and committed before the next:

- A. Co-save record version 4: per-row outfit lists, the random flag, the chosen outfit
  (§1). High impact (SKSE serialization).
- B. `situations.json`: situation names and descriptions, removed situations, the
  descriptions in sentences (§2). High impact (a new file write shared by every save).
- C. The Manage Situations page (§3-§6), and Manage Wardrobe's unsaved-changes alert
  aligned with it.

Docs follow the code in the step that makes them stale: design.md (§5 co-save, §7
situations), README, docs/outfit-management-ui.md, docs/ui-redesign.md, CLAUDE.md §2.1.

Terms: a *row* is the Default or one situation, per actor. The *preset situations* are
the ones Livery ships: Default, Town, Home, Sleep, Swimming, Warm (`Situations::kChoosable`
plus Default).

## 1. Rows (step A)

Per actor, per row:

- `outfits`: library outfit ids, in the user's order. The order means nothing to what is
  worn; it is only how the page lists them.
- `random`: off by default.
- `chosen`: the id worn with random off, 0 for none. It is always on `outfits` or 0.
- The Default only: `pieces`, armor not in the wardrobe (what `Assign(pieces, 0)` gives
  today).

What a row wears:

- Random off: `chosen`. The Default with `chosen` 0: its `pieces`.
- Random on: one of `outfits`, picked when the actor starts wearing the row (the resolved
  row changes, edge-triggered as the poll is) and kept until it changes again; the record
  holds the picked id (`wornOutfit`, since `chosen` cannot tell it). Any outfit of the list
  may be picked, repeats included. The Default with an empty list: its `pieces`.
- A row *has an outfit* when it wears something by these rules; `Situations::Applicable`
  is walked as today, the first row that has one is worn, else the Default, else their own
  clothes.
- A row *holds* an outfit when it is `chosen` (random off) or on `outfits` (random on).
  "Holds" is what the Save page's `wears it for` ticks, Manage Wardrobe's `Situations for
  #{Name}`, the outfit list's Situation tags, headings and star mark, and Remove's popup show.

Every row reads its outfit from the library each time it goes on, the Default included;
only the Default's `pieces` are kept as given.

Changes:

- Giving a row an outfit (a tick on the Save page or Manage Wardrobe, Apply, Apply to
  #{Name}, a drop on the page): it goes to the front of `outfits`, or moves there if it is
  on it already; random off, it becomes `chosen`, with no question. Apply, Apply to #{Name}
  and the Save page then dress the actor; with random on and the row worn now, it is worn
  as the pick.
- A library outfit made the Default's `chosen` drops the Default's `pieces`.
- Unticking a row on the Save page or Manage Wardrobe takes the outfit off `outfits`;
  `chosen` becomes 0 if it was that.
- The Save page's and Manage Wardrobe's taken-row line reads `Town: "B" replaces "A"` (random
  off, another `chosen`); the buttons are `Confirm` / `Cancel`. `Replace` and `Keep` go,
  since the replaced outfit stays on the list, hidden. A random-on row reads `Town: added`.
- The Inventory page's Apply popup: `No, apply as is (?)` (was `No, just apply`). The ticks
  become the Default's `pieces`, `chosen` 0, random off; `outfits` stays, hidden. Its help:
  `The ticked pieces become #{Name}'s Default as they are, without being added to the
  wardrobe.`, then, while a situation row holds an outfit, `While a situational outfit is
  worn, the new Default shows only when that situation ends.`
- Remove (`Assignments::ForgetOutfit`): the outfit leaves every row's `outfits`; a `chosen`
  that was it becomes 0, so an outfit hidden behind it is not worn. When this leaves the
  Default wearing nothing (random off: `chosen` was it; random on: it was the only one on
  the list), the outfit's pieces become the Default's `pieces`, read before
  `Library::Remove`. Those wearing it are redressed as today (outfit-management-ui.md §5).
- Update of an outfit: whoever wears it, for any row, wears the new pieces at their next
  redress; the captured actor at once by docs/outfit-management-ui.md §6.
- A redress forced by a change (`Assignments::Change`, `Refresh`) keeps a random row's pick
  while it is still on the list, else picks again.

Co-save version 4: each assignment's record as version 3, plus `wornOutfit`, then for each
row its situation (the `Situations::Situation` value), `random`, `chosen`, `outfits`; the
Default's `pieces` where version 3 kept the default armor. Reading version 3: a default with
an outfit id still in the library becomes `outfits = [id]`, `chosen = id` and its armor is
dropped; with id 0, or an id the library no longer has, the armor becomes `pieces`; each
situation id becomes `outfits = [id]`, `chosen = id`; random off everywhere.

## 2. Situation data (step B)

`Data/SKSE/Plugins/Livery/situations.json`, global like `outfits.json`:

```json
{ "version": 1, "situations": [ { "key": "Town", "name": "Town", "description": "in Town", "removed": false } ] }
```

- `key` is the `Situations::Situation` it is for (`Default`, `Town`, `Home`, `Sleep`,
  `Swimming`, `Warm`), never shown or changed. The file holds only rows that differ from
  the presets; a missing file means every preset as installed and is not written at load.
- Presets: name as the key; descriptions `in Town`, `at Home`, `ready for Sleep`,
  `swimming`, `keeping Warm`, and an empty one for the Default (it is worn in towns without
  a Town outfit and in fights too, so no phrase fits every place).
- Names: never empty; unique among the situations not removed, ASCII case ignored. The
  Default cannot be renamed or removed.
- The same write rules as `outfits.json` (CLAUDE.md §2.1): `Load` sets the writable state
  once and never writes a file it could not read or that a newer version wrote; writes go
  through a temporary file and a rename; game thread only; only after the user's action
  (§5, §6).
- A removed situation offers no row anywhere: not on the Save page's `wears it for`, Manage
  Wardrobe's dropdown, the Situation category group, the sort's headings, or the page's
  list; `Situations::Evaluate` still reports it and nothing wears it. Removing it clears
  its rows in this save (§5); another save's rows for it are dropped when that save loads,
  with a log line. A save not loaded between Remove and Restore Presets keeps its rows.
- The names replace `Situations::Name` wherever the window and the HUD show a situation;
  logs keep the key.

Descriptions in sentences: `, <description>` at the end, nothing when the description is
empty. Only these:

| Where | Now | With the description |
| --- | --- | --- |
| Inventory summary line (`DescribeAssignment`) | `Dressed by Livery in "X" for Town.` | `Dressed by Livery in "X", in Town.` |
| Situation change HUD (`SituationChangeText`) | `Livery: Lydia now wears "X" for Town.` | `Livery: Lydia now wears "X", in Town.` |
| Assign HUD, nothing went on | `...; dressed for Town now.` | `...; dressed now, in Town.` |
| Assign HUD | `Livery: Lydia now wears 5 piece(s) for Town` | `Livery: Lydia now wears 5 piece(s), in Town` |
| Change HUD, the end | `. Dressed for Town now, 5 piece(s)` | `. Dressed now, in Town, 5 piece(s)` |
| Refresh HUD | `Livery: Lydia now wears "X" for Town, 5 piece(s)` | `Livery: Lydia now wears "X", in Town, 5 piece(s)` |

The names stay in: the Apply popup's `"X" is Lydia's Town outfit.`, the star's `For Town,
Warm`, the Situation tags and headings, the rows' labels and change lines (`Town: added`),
Remove's popup (`Lydia: Default, Town`), the Change HUD's `"X" for Town, Warm` and `no
outfit for Town any more`.

## 3. Manage Situations page (step C)

- The third tab of the tab column (docs/outfit-management-ui.md §3): a simple snowflake,
  tooltip `Manage Situations`. The tab column shows on the Inventory, Manage Wardrobe and
  Manage Situations pages.
- Title `#{Name}'s Situational Outfits`, no summary line. Two panes as on Manage Wardrobe:
  the Situation List (§4) on the left, the outfit list or the selected situation (§5) on the
  right.
- The preview shows the Inventory page's ticks.
- Every change on the page is an edit until Save Changes (§6), except Remove Situation and
  Restore Presets, which write at once after their popups.

## 4. Situation List (left pane)

- Heading `Situation List (?)` (`Theme::ListHeadingHelp`): `Each situation lists the outfits
  #{Name} can wear there; the first one that applies is worn, else the Default. Drag an
  outfit from the outfit list onto a situation to add it, between two of its outfits to place
  it, onto another situation to move it, or back to the outfit list to take it off.`
- One heading per situation not removed, the Default first, then `Situations::kChoosable`
  order. Each: the name in the outfit list's row style; `(n)`, the length of `outfits`, in
  the small heading style; a chevron, pointing down while open and right while folded, grey,
  white while hovered; then a rule in the list's rule colour to the pane's right end.
  Every heading is open when the page opens.
- Click areas: from the chevron (with its padding) to the right end, rule included, folds;
  left of it selects. A click selects the situation (the single-choice row highlight of the
  skin) and shows its details on the right; a click on the selected one unselects it and the
  right pane shows the outfit list.
- Under a heading, its outfits as rows of the outfit list (name, tags, marks), each with a
  drag grip at its left: two columns of three dots, Vanilla and Nordic in LabelText's grey at
  low alpha, Tailor in its text colour mixed with black. Random on: every outfit on the list.
  Random off: `chosen` only. A row that wears nothing shows a grey `No outfit chosen` line;
  the Default wearing its `pieces` shows a `Pieces not in the wardrobe` line with no grip.
  A click on an outfit row does nothing.

Drag and drop (`ImGui::BeginDragDropSource` / `Target`):

| From | Dropped on | Result |
| --- | --- | --- |
| an outfit list row (right) | a heading, its rule, its `No outfit chosen` or `Pieces...` line | added at the front (§1) |
| an outfit list row (right) | between two outfit rows of a situation | added there |
| a situation's outfit row | another situation, as above | moved: off the source row (its `chosen` 0 if it was that), added to the target |
| a situation's outfit row | its own situation | reordered |
| a situation's outfit row, or `Pieces not in the wardrobe` | the right pane | taken off that row (`chosen` 0 if it was that; the `pieces` dropped) |

- An outfit the target already holds moves to the drop position instead of being added
  twice. With random off, the outfit dropped becomes `chosen`.
- A folded heading opens after the drag hovers it for `kDragExpandDelay` (1 s, an
  anonymous-namespace constant in the page's file); a drop before that acts the same.
- While a drag from the left pane is on, the right pane shows the outfit list; when it
  ends, the right pane follows the rule in §5.
- The outfit list's drags add copies: the right pane keeps every outfit.

## 5. Right pane

It shows the selected situation's details while one is selected and Back to Outfit List
was not pressed since it was selected, and no drag from the left pane is on; else the
outfit list.

The outfit list: the shared outfit list (docs/outfit-management-ui.md §1) with its own
search, sort and Show Categories, each row with the drag grip. A click selects nothing.

The details:

- `Global Settings`, in the small heading style with a rule to the pane's end, then:
  - `Name`, an underlined field (as the Save page's). Ending the edit with it empty, or with
    a name another situation has (ASCII case ignored), puts the previous name back; with
    that empty too, `Unnamed Situation`. The Default's is read only.
  - `Description`, an underlined field, empty allowed; under it, in the small heading style,
    `Shown as: Dressed by Livery in "Outfit", <description>.` (without the comma part when
    empty).
  - `Conditions`, read only until conditions can be edited (design.md §8): the preset's rule
    in grey, as the README words it (Town: `A city, town, settlement, inn or other
    inhabited interior.`); the Default's `Worn where no other situation applies.`
- `#{Name}'s Settings`, the same heading style and rule, then `Random Outfit (?)`, a
  checkbox: `On: one outfit of the list is picked at random each time this situation begins
  for #{Name}, and worn until it ends. Off: only the outfit shown is worn; the others stay
  on the list, hidden.`
- Centred in the pane, a borderless `Back to Outfit List`; under it, in the small heading
  style, `Drag outfits from the list onto "#{Situation}" to add them.` A click shows the
  outfit list, keeps the situation selected, opens its heading if folded and scrolls the
  left pane to it when the pane can scroll.
- At the pane's bottom, `Remove Situation`, drawn as Restore Original Outfit is, without the
  `(?)`; not on the Default. Its popup: `Remove "Town"?`, the actors in this save whose
  Town row holds anything (`Lydia: 2 outfit(s)`; a FormID when not in memory), `Restore
  Presets brings "Town" back, without them.`, and with unsaved edits `Your unsaved changes
  are discarded.`; `Remove` / `Cancel`. Remove writes `situations.json` first; a refused or
  failed write changes nothing and shows the error. Then `Assignments::ForgetSituation`
  clears the row for every actor in this save, and those wearing it are redressed by
  docs/outfit-management-ui.md §6 (the others in memory at close, the rest when they load).
  The edits are dropped, nothing is selected and the right pane shows the outfit list.

## 6. Buttons and leaving

- `Restore Presets (?)` at the row's left end (`Theme::HelpButton`), then `Save Changes` and
  `Exit` where the other pages put their buttons.
- Restore Presets: greyed, with `Every preset situation is as installed.`, while they are.
  Its help: `Brings the preset situations back as installed: a removed one returns, empty,
  and a renamed or redescribed one gets its words back. Nobody's outfits change.` It always
  asks: the changes (`Town: restored`, `Home: name reset`, `Warm: name and description
  reset`) and, with unsaved edits, `Your unsaved changes are discarded.`; `Restore` /
  `Cancel`. Restore writes `situations.json` (a refused or failed write changes nothing and
  shows the error), drops the edits and keeps the selection.
- Save Changes: greyed with `Nothing has changed.` until the edits differ from what is
  saved. It writes the global edits to `situations.json` first, when there are any; a
  refused or failed write changes no row and shows the error. Then it makes the captured
  actor's row changes in one call that dresses them once, by docs/outfit-management-ui.md
  §6 (at once with Live Preview on, at close with it off). The page stays, the edits start
  again from what was saved.
- Exit closes the window; Escape does what Exit does, first clearing a search.
- Exit, Escape or another tab with unsaved edits asks `You have unsaved changes.` with
  `Save Changes` / `Discard Changes` / `Back`: Save Changes saves as the button does, then
  goes; a refused save stays. Discard Changes drops the edits and goes. Back stays.
- Manage Wardrobe asks the same on Exit, Escape and the Inventory tab (replacing `Discard
  changes and exit` / `Back` and `OK` / `Back`); its Save Changes runs Update, popup
  included. Its `Discard changes` / `Keep editing` on selecting another outfit stays.

## 7. Code

- `src/SituationStore.cpp` / `.h` (step B): load, names, descriptions, removed, Rename /
  Describe / Remove / RestorePresets writes, the presets as installed. Unit tests: presets,
  the name rules, a file with unknown keys, Restore's change lines.
- `src/Assignments.cpp` (step A): the rows, version 4 and the version 3 read, the pick,
  "has an outfit" and "holds", `ForgetOutfit`'s rules; step B: `ForgetSituation` and the
  rows of removed situations dropped at load. The row rules that need no game (insert at
  front, move, `chosen` on remove, resolve) in a header the unit tests reach.
- `src/SituationPage.cpp` (step C): the page, its drag and drop, popups and buttons;
  `CaptureWindowInternal.h` gains the page and its state; `CMakeLists.txt` gains the files
  (CLAUDE.md §1, build system).
- `Theme`: the snowflake tab icon, the Situation List heading (right-hand chevron with its
  own click area; `FoldHeading` stays as it is), the drag grip, the small heading with a rule.
- `CaptureWindow::Request`: the situations offered (key, name, description, conditions
  text) from the store; each row's `outfits`, `random`, `chosen` for the captured actor;
  `onSituationsSave`, `onSituationRemove`, `onRestorePresets`, each writing first by §5/§6.
- `Plugin.cpp`: the handlers, the redress queue (docs/outfit-management-ui.md §6), the
  description sentences.

CLAUDE.md §2.1 gains, in step B: `situations.json` (`src/SituationStore.cpp`) is shared by
every save too; a situation's name or description changes only through Manage Situations'
Save Changes, a situation is removed only through Remove Situation's popup and restored
only through Restore Presets' popup; every write is made before any assignment changes, a
refused or failed one changes none; the same load and write rules as the library.

## 8. Checks

- Step A: a version 3 save loads with every assignment as it was; save and load again; the
  Save page's tick of a taken row (`"B" replaces "A"`, Confirm, Cancel); Remove of a chosen
  outfit with another hidden behind it wears neither; Remove of the Default's outfit keeps
  its pieces on; `No, apply as is`; Update of an outfit an NPC wears for a situation shows at
  their next change. Unit tests cover the row rules.
- Step B: no `situations.json` until something is written; the summary line and the HUD
  read `, in Town`; an empty description adds nothing.
- Step C: each drop of §4's table; the 1 s open; Back's scroll and open; Save Changes greyed
  and saved (Live Preview on and off); Random Outfit on, picks at situation changes;
  rename refused when empty or taken; Remove Situation and its popup, the rows gone, the
  Save page and Manage Wardrobe without it; Restore Presets brings it back empty; the
  unsaved alert on Exit, Escape and both tabs, on both pages; the three skins.

## 9. Later

- Editing conditions; user-defined situations (design.md §8 item 9).
