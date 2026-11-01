# Situation management

Written 2026-10-21 for the AI that implements it, from the user's draft and the decisions
made on it; current spec only:

- The rows: per-row outfit lists, the random flag, the chosen outfit (§1); the co-save
  record that keeps them is in design.md §5.
- `situations.json`: situation names, removed situations, the names in sentences (§2).
- The Situations page (§3-§6).

Terms: a *row* is the Default or one situation, per actor. The *preset situations* are
the ones Livery ships: Default, Town, Castle, Home, Sleep, Cold (`Situations::kChoosable`
plus Default).

## 1. Rows

Per actor, per row:

- `outfits`: library outfit ids, in the user's order. The order means nothing to what is
  worn; it is only how the page lists them.
- `random`: off by default.
- `chosen`: the id worn with random off, 0 for none. With random on, the last pick worn,
  recorded by `Assignments` as the row goes on. It is always on `outfits` or 0.
- The Default only: `pieces`, armor not in the wardrobe (what `Assign(pieces, 0)` gives
  today).
- `excluded`: per outfit on `outfits`, the armor FormIDs it leaves off for this actor
  (`OutfitRows::Exclusion`; design.md §2). Set by Apply to #{Name} from the Wardrobe page's
  ticks and by the ticks of an outfit's details opened from the row (§5); the Dress
  page's Apply and Create and Apply give the outfit with all its items. An outfit that
  leaves `outfits` (taken off, or replaced as `chosen`) loses them. Kept in the co-save's
  `EXCL` record (design.md §5).

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
  "Holds" is what the outfit list's Situation headings and star mark, and Remove's popup
  show.

Every row reads its outfit from the library each time it goes on, the Default included;
only the Default's `pieces` are kept as given.

Changes:

- Giving a row an outfit (Apply, Apply to #{Name}, Create and Apply, a drop on the page; `OutfitRows::Give`, `Place`): random on, it goes to the front
  of `outfits` (a drop: the drop place), or moves there if it is on it already. Random off
  the list keeps its length: an outfit on it becomes `chosen` where it is; one not on it
  takes `chosen`'s place, and the replaced outfit leaves the list; with no `chosen` it goes
  in at the front (a drop: the drop place). Either way it is `chosen`, with no question.
  Apply, Apply to #{Name} and Create and Apply then dress the actor; with random on and the row
  worn now, it is worn as the pick.
- A library outfit made the Default's `chosen` with random off drops the Default's `pieces`.
- Turning random off (`OutfitRows::SetRandom`) keeps `chosen` on: the last pick worn, or the
  outfit chosen before random went on; with none, the first of the list.
- Taking an outfit off a row (a drag off it on the page; `OutfitRows::Take`) takes it off
  `outfits`; `chosen` becomes 0 if it was that.
- The Dress page's Apply popup: `No, apply as is (?)`. The ticks
  become the Default's `pieces`, `chosen` 0, random off; `outfits` stays, hidden. Its help:
  `The selected items become #{Name}'s Default as they are, without being added to the
  wardrobe.`, then, while a situation row holds an outfit, `While a situational outfit is
  worn, the new Default shows only when that situation ends.`
- Remove (`Assignments::ForgetOutfit`): the outfit leaves every row's `outfits`; a `chosen`
  that was it becomes 0, so an outfit hidden behind it is not worn. When this leaves the
  Default wearing nothing (random off: `chosen` was it; random on: it was the only one on
  the list), the outfit's pieces become the Default's `pieces`, read before
  `Library::Remove`. Those wearing it are redressed as today (outfit-management-ui.md §5).
- Another save's rows still naming a removed outfit keep its id in the co-save, so restoring
  outfits.json brings it back. Until then the id counts as not on the row
  (`OutfitRows::WithoutGone`): the window never shows it or names it, Remove Situation's
  popup does not count it, a random row never picks it, and a situation row with only such
  ids is passed over as one without an outfit. The page's save keeps it (`OutfitRows::KeepGone`);
  an outfit given in its `chosen` place leaves it on the list.
- Update of an outfit: whoever wears it, for any row, wears the new pieces at their next
  redress; the captured actor at once by docs/outfit-management-ui.md §6.
- A redress forced by a change (`Assignments::Change`, `Refresh`) keeps a random row's pick
  while it is still on the list, else picks again.

The co-save keeps the rows in the `ASGN` record (version 5, situations by key) and the
items left off in `EXCL`; the layout and how older versions are read are in design.md §5.

## 2. Situation data

`Data/SKSE/Plugins/Livery/situations.json`, global like `outfits.json`:

```json
{ "version": 1, "situations": [ { "key": "Town", "name": "City", "removed": false } ] }
```

- `key` is the `Situations::Situation` it is for (`Default`, `Town`, `Castle`, `Home`,
  `Sleep`, `Cold`; `Warm` is read as `Cold`), never shown or changed. The file holds only rows that differ from
  the presets; a missing or empty file (nothing but whitespace) means every preset as
  installed and is not written at load. An entry's other fields (such as a `description`)
  are ignored, and a rewrite drops them.
- Presets: name as the key.
- Names: never empty; unique among the situations not removed, ASCII case ignored. The
  Default cannot be renamed or removed.
- The same write rules as `outfits.json` (CLAUDE.md §2.1): `Load` sets the writable state
  once and never writes a file it could not read or that a newer version wrote; writes go
  through a temporary file and a rename; game thread only; only after the user's action
  (§5, §6).
- A removed situation offers no row anywhere: not in an outfit's details, the Situation
  category group, the sort's headings, or the page's
  list; `Situations::Evaluate` still reports it and nothing wears it. Removing it clears
  its rows in this save (§5); another save's rows for it are dropped when that save loads,
  with a log line. A save not loaded between Remove and Restore Presets keeps its rows.
- The names replace `Situations::Name` wherever the window and the HUD show a situation;
  logs keep the key.

The sentences that say what is worn end in ` for <name>` (`SituationStore::Suffix`),
nothing for the Default; the summary line names the Default too. Only these:

| Where | Text |
| --- | --- |
| Inventory summary line (`DescribeAssignment`) | `Wearing X for Town.`, `Wearing X as Default.`, each name with its icon in the palette's `Emphasis` colour (`Theme::SubtitleRun`); `Dressed by Livery.` for pieces that are not a saved outfit or an outfit gone from the library; with pieces Livery put on no longer worn (`PiecesNotWorn`), `Assigned X as Default, 1 item not worn.`, `Dressed by Livery, 2 items not worn.`; with worn armor Livery did not put on, `, 1 extra item worn` after them (`GapsOf`) |
| Situation change HUD (`SituationChangeText`) | `Livery: Lydia now wears "X" for Town.` |
| Assign HUD, nothing went on | `...; dressed now for Town.` |
| Assign HUD | `Livery: Lydia now wears these for Town` |
| Change HUD, the end | `. Dressed now for Town` |
| Refresh HUD | `Livery: Lydia now wears "X" for Town` |

The names also show in: the Apply popup's `"X" is Lydia's Town outfit.`, the star's `For Town,
Cold`, the Situation headings, the rows' labels and change lines (`Town: added`),
Remove's popup (`Lydia: Default, Town`), the Change HUD's `"X" for Town, Cold` and `no
outfit for Town any more`.

## 3. The Situations page

- The third tab of the tab column (docs/outfit-management-ui.md §3): the star, point down,
  that marks a situational outfit in the outfit list, tooltip `Situations`.
- Title `Situational Outfits (#{Name})` after the star, no summary line. Two panes as on the Wardrobe page:
  the Situation List (§4) on the left, the outfit list or the selected situation (§5) on the
  right.
- The preview shows the Dress page's ticks, or the outfit whose details show (§5).
- Every change on the page is an edit, saved shortly after the last one or as the page is
  left (§6); the details' Remove and Restore Presets save the edits, then write after their
  popups.

## 4. Situation List (left pane)

- Label `Situation List (?)` above the list (`Theme::LabelText` + `LabelHelpMark`):
  `Each situation lists the outfits #{Name} can wear there. When several
  apply, the most specific one with an outfit is worn; otherwise, and in combat, the Default.
  Drag an outfit onto a situation to add it, between its outfits to place it, onto another
  situation to move it, or back to the outfit list to take it off.` (The most specific:
  `Situations::Evaluate`'s order.)
- One heading per situation not removed, the Default first, then `Situations::kChoosable`
  order. Each: the star of the Situations tab, a square 4px short of the name's line height in the name's colour, 14px in from the row's left edge, then 6px on the name in the outfit list's row style,
  which leaves the select mark its room; with
  random on, a die (a hexagon split into three faces with one, three and two pips, a square 4px
  short of the name's line height, grey) and `(n)`, the length of `outfits`, in the small heading style,
  the die's outline as far from the name and from `(n)` as `(n)` is from a name without it
  (`ItemInnerSpacing.x`); with random on and anything showing under it, a chevron, pointing
  down while open and right while folded, grey, lit while hovered (white in Vanilla and
  Nordic, the accent in Modern); then, 12px on, a rule in the list's rule colour, and 12px on
  `Edit` at the pane's right end, in the small heading style, lit as the chevron is. A heading
  without a chevron is never folded: random off shows its one outfit; a heading with nothing
  under it wears nothing and its name is greyed. Every heading is open when the page opens.
- Click areas: from the chevron (with its padding) to Edit, rule included, folds; left of it
  selects (with no chevron, everything left of Edit selects); Edit with its padding to the
  right end. A click on the name selects the situation (the single-choice row highlight of the
  skin) and shows its details on the right; a click on the selected one while its details
  show unselects it and the right pane shows the outfit list. Edit always selects the
  situation and shows its details.
- Under a heading, its outfits as rows of the outfit list (name, the form marks but no
  star, since each is chosen for a situation), each with a drag grip: two columns of three
  dots, in LabelText's grey at low alpha, here level with the headings' names (`Theme::Grip::kName`), in the right pane's
  outfit list where its fold headings' text starts, after the chevron (`kEdge`); the name
  starts 12px past the dots. Random on: every outfit on the list. Random off: `chosen`
  only. A row that wears nothing shows nothing under its heading; the Default wearing its
  `pieces` shows a `Items not in the wardrobe` line with no grip; an id the library no longer
  has (another save's) shows nothing (§1).
- A click on an outfit row, here or in the right pane's outfit list, shows the outfit's
  details in the right pane (§5) and puts it on the preview; a click on the outfit whose
  details show goes back, as Back does. Its rows in both panes show as selected meanwhile
  (docs/outfit-management-ui.md §1): in Vanilla and Nordic the equipped mark sits left of the
  dots here, and in the right pane takes the dots' place, shrunk to leave 2px on either side.
  A click on the pieces line does nothing. A drag never counts as a click.

Drag and drop (`ImGui::BeginDragDropSource` / `Target`):

| From | Dropped on | Result |
| --- | --- | --- |
| an outfit list row (right) | a heading, its rule, its `Items...` line | added at the front (§1) |
| an outfit list row (right) | between two outfit rows of a situation | added there |
| a situation's outfit row | another situation, as above | moved: off the source row (its `chosen` 0 if it was that), added to the target with the items it left off there, unless the target has it already; its details, when shown from the source, follow it |
| a situation's outfit row | its own situation | reordered |
| a situation's outfit row, or `Items not in the wardrobe` | the right pane | taken off that row (`chosen` 0 if it was that; the `pieces` dropped) |

- Random on: an outfit the target already holds moves to the drop position instead of being
  added twice. Random off: the outfit dropped becomes `chosen` by §1's rule, the one it
  replaces leaving the list, so the list keeps its length.
- Between two outfit rows (random on): over a row's upper half the drop goes before it,
  over its lower half after it. With random off a drop on a situation shows only `chosen`.
  While a drop would land, a line in the accent colour shows where, across the pane; over
  the right pane, a frame in that colour around it.
- While dragged, a see-through copy of the row (`Theme::OutfitDragGhost`) follows the mouse,
  held where the row was grabbed: the popup background, the grip's dots and the name, no
  fills or marks in any skin; the pieces line's copy is greyed as the line is. No tooltip.
- A folded heading opens after the drag hovers it for `kDragExpandDelay` (1 s, an
  anonymous-namespace constant in the page's file); a drop before that acts the same.
- While a drag from the left pane is on, the right pane shows the outfit list; when it
  ends, the right pane follows the rule in §5.
- The outfit list's drags add copies: the right pane keeps every outfit.

## 5. Right pane

It shows the outfit list while a drag from the left pane is on; else the details of the
outfit clicked last, until Back; else the selected situation's details, until Show Outfit
List; else the outfit list.

The outfit list: the shared outfit list (docs/outfit-management-ui.md §1) with its own
search and sort, each row with the drag grip, and nothing under the list. Its star and
Default marks (and the star's `For Town, Cold`) follow the page's edits, saved or not, at
once; in the Situation List too, the Default mark does. While
an outfit's details show, the
selected situation's heading is drawn unselected (no fill, no mark); closing them brings it
back.

The situation's details, its fields evenly spaced with no headings:

- `Name`, an underlined field (as the Save page's), its label starting with the star of the
  Situations tab, a square of the label's line height in its grey, and 6px (`Theme::LabelText`'s
  `star`). Ending the edit with it empty, or with a
  name another situation has (ASCII case ignored), puts the previous name back; with that
  empty too, `Unnamed Situation`. Enter or the field losing focus ends the edit, and the name
  is saved as any edit is; no save starts while it is being typed. The Default's is read only, greyed, and says `The Default's
  name cannot be changed.` on hover.
- `Conditions`, read only until conditions can be edited (design.md §8): the preset's rule in
  grey, as the README words it (Town: `A city, town, settlement, inn or other inhabited
  interior.`); the Default's `Worn where no other situation applies.`
- `Random Outfit (for #{Name}) (?)`, a checkbox: `On: one outfit of the list is picked at random
  each time this situation begins for #{Name}, and worn until it ends. Off: only the outfit
  shown is worn, the last one picked; the others stay on the list, hidden.` Unticking it keeps
  the last pick on (§1).
- 12px of extra room, then `Show Outfit List`, a button centred in the pane. A click shows
  the outfit list, keeps the situation selected, opens its heading if folded and scrolls the
  left pane to it when the pane can scroll.
- `Remove` at the right end of Show Outfit List's row, drawn as Restore Presets is, without
  the `(?)` (`Theme::LineLink`); not on the Default. Its popup: `Remove "Town"?`, then under `It is used by:` the
  actors in this save with outfits on their Town row's list, hidden ones included
  (`Lydia: 2 outfit(s)`, the length of `outfits`; a FormID when not in memory), `Restore
  Presets brings "Town" back, without them.` (`... back.` when nobody has any); `Remove` /
  `Cancel`. The page's edits are saved first, and a refused save opens no popup. Remove writes `situations.json` first; a refused or
  failed write changes nothing and shows the error. Then `Assignments::ForgetSituation`
  clears the row for every actor in this save, and those wearing it are redressed by
  docs/outfit-management-ui.md §6 (the others in memory at close, the rest when they load).
  Nothing is selected and the right pane shows the outfit list.

An outfit's details, in the Wardrobe page's order (`DrawOutfitSummary`): its icon and name
with no label (`Theme::OutfitNameText`), `SITUATIONS (#{Name})`, its label starting with the
star as the Name label's does, with the rows the page's edits
give it (`Default, Town`, or `None`), then its pieces, with the Dress page's icons, under the
`Name` and `Type` labels that sort them as on the Wardrobe page, over their count line. Clicked
under a situation's heading, the pieces are the Wardrobe page's tick rows, ticked as that row wears the
outfit; a tick is an edit of that row (§1 `excluded`), saved as any edit is, and the last
ticked piece cannot be unticked. Clicked on the outfit list, or once that row no longer
holds the outfit, they are read only: every row ticked, taking no click, drawn ungreyed. The outfit's row in the Situation List is drawn
selected only under the heading it was clicked under. The preview shows the outfit less its
unticked pieces. 12px of extra
room, then `Open in Wardrobe` (wardrobe icon before its label) and `Back` centred; when they
would leave less than one item spacing on either side (`ItemSpacing.x`), they go one over the
other, as far apart as buttons side by side are. The details grow with the pieces, moving the page's
buttons down when taller than the Situation List, as the Wardrobe page's selection does; the
pieces' list scrolls only when the whole details would pass 60% of the screen (the cap every
list keeps to, `kMaxListHeightRatio`), so the window stays on screen. Back, and Escape, show
what the right pane showed before and the Dress page's ticks on the preview. Open in Wardrobe goes to
the Wardrobe page with the outfit selected and on the preview, once the page's edits are saved
(§6). A click on a situation's heading closes the details and
shows the situation's.

## 6. Buttons and leaving

- `Exit` where the other pages put their buttons, and at the row's right
  end `Restore Presets (?)`, a link drawn as the details' Remove and Restore are, its
  `(?)` inside its click area, its text centred on the buttons' height
  (`Theme::LineHelpLink`).
- Restore Presets: greyed, with `Every preset situation is as installed.`, while they are.
  Its help: `Brings the preset situations back as installed: a removed one returns, empty,
  and a renamed one gets its name back. Nobody's outfits change.` It always asks: the
  changes (`Town: restored`, `Home: name reset`); `Restore` / `Cancel`. The page's edits are
  saved first, and a refused save opens no popup. Restore writes `situations.json` (a refused
  or failed write changes nothing and shows the error) and keeps the selection.
- Saving: each edit (a drop, Random Outfit, a tick of an outfit's details, a name entered)
  starts the wait of `kAutosaveDelay` (0.5 s) again, so a burst of edits writes and redresses
  once (`ScheduleAutosave`, `TakeAutosaveDue`). A drag, an open popup or a name being typed
  holds the save until it ends. The page takes no input while a save runs but keeps its look
  (`BeginPageDisabled`, `State::quietSave`), and the HUD says nothing
  of a rename (the log does); the redress has its usual HUD message. A save writes the global edits to `situations.json` first, when there are any; a
  refused or failed write changes no row and shows the error. Then, when a row changed, it
  replaces the captured actor's rows at once (`Assignments::SetRows`, so the page shows them
  even before the redress) and dresses them once (`Assignments::Refresh`), by
  docs/outfit-management-ui.md §6 (at once, Live Preview on or off; with it on the preview
  begins again on the outfit whose details show (§5), else on what they wear); a random
  row they wear wears the outfit it gained first. The page stays, an outfit's details
  too, and the edits start again from what was saved.
- Exit closes the window; Escape does what Exit does, first clearing a search. While a write
  runs (a save, Remove, Restore Presets) the page takes no input, Remove and Restore Presets
  greying it, and Escape does nothing,
  here or as an outfit's Back.
- Exit, Escape, another tab or an outfit's Open in Wardrobe with edits not saved yet saves them at once
  and goes once the write took; no alert asks. A refused save stays and shows the error, and
  leaving again with the error showing drops the edits and goes, so a write that keeps failing
  cannot hold the window open.

## 7. Code

- `src/SituationStore.cpp` / `.h`: load, names, removed, the
  `Apply` (names of several situations in one write) / `Remove` /
  `RestorePresets` writes, Restore's change lines. The rules that need no game (the presets,
  the file's parse and write, the name rules, Restore's change lines) in
  `src/SituationTable.h`, which `test/SituationStoreTests.cpp` reaches: presets, the name
  rules, a file with unknown keys, Restore's change lines.
- `src/Assignments.cpp`: the rows, the co-save record and the older versions' read, the
  pick, "has an outfit" and "holds", `ForgetOutfit`'s rules, `ForgetSituation` and the rows
  of removed situations dropped at load. The row rules that need no game (insert at front,
  move, `chosen` on remove, resolve) are in `src/OutfitRows.h`, which
  `test/OutfitRowsTests.cpp` reaches.
- `src/SituationPage.cpp`: the page, its drag and drop, popups and buttons;
  `CaptureWindowInternal.h` holds the page and its state. `CaptureWindow.cpp` has the tab
  column's tabs and what leaving a page does (`RequestLeave`, `FinishLeave`), with the
  Wardrobe page's unsaved-changes alert (`DrawLeavePopup`).
- `src/OutfitRows.h`: `Place`, a drop at a place in a row's list (`Give` is a drop at the
  front); `src/Assignments.cpp`: `SetRows`, `HoldersOf`, `Refresh`'s pick.
- `Theme`: the tab icon (the star of `OutfitMarks`), the Situation List heading
  (`SituationHeading` with its `SituationHeadingLook`, the die of random on, right-hand chevron and Edit with their
  own click areas, `HeadingClick::kEdit`), the
  drag grip (`Theme::Grip` for `OutfitRow`), the
  details' Remove link (`LineLink`), Restore Presets' (`LineHelpLink`).
- `CaptureWindow::Request`: the situations offered (`SituationInfo`: key, name,
  conditions text, who has outfits on it) from the store; each row's `outfits`, `random`,
  `chosen` for the captured actor; `onSituationsSave`, `onSituationRemove`,
  `onRestorePresets`, each writing first by §5/§6.
- `Plugin.cpp`: the handlers, the redress queue (docs/outfit-management-ui.md §6), the
  summary line.

## 8. Checks

- Rows: an older co-save loads with every assignment as it was; save and load again; Remove
  of a chosen outfit with another hidden behind it wears neither; Remove of the Default's
  outfit keeps its pieces on; `No, apply as is`; Update of an outfit an NPC wears for a
  situation shows at their next change. Unit tests cover the row rules.
- Situation data: no `situations.json` until something is written; the summary line and the
  HUD read ` for Town`; the summary line `as Default` for the Default.
- The page: each drop of §4's table; the 1 s open; Show Outfit List's scroll and open; Edit and
  a click on the selected heading with the outfit list showing; an outfit's details from
  either pane, with Back, Escape and Open in Wardrobe, the labels' sort, both button layouts, and the preview; several drops in a row redressing
  once, about 0.5 s after the last (Live Preview on and off); Random Outfit on, picks at situation changes;
  rename refused when empty or taken; Remove and its popup, the rows gone; Restore Presets brings it back empty; the
  Exit, Escape and a tab right after an edit, saving before they go; the three skins.

## 9. Later

- Editing conditions; user-defined situations (design.md §8). The Default's
  conditions stay read only then, saying so on hover as its name does (§5).
