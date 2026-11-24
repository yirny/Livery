# Situation management

Written 2026-10-21 for the AI that implements it, from the user's draft and the decisions
made on it; current spec only:

- The rows: per-row outfit lists, the random flag, the chosen outfit, the selection
  chances (§1); the co-save records that keep them are in design.md §5.
- `situations.json`: situation names, removed situations, the names in sentences (§2); its
  version 2 (the user's situations, conditions, the order) is in docs/custom-situations.md §6.
- The Situations page (§3-§6).

Terms: a *row* is the Default or one situation, per actor. The *preset situations* are
the ones Livery ships: Default, Town, Castle, Home, Sleep, Cold (the ids up to
`Situations::kPresetCount`); the others are the user's (docs/custom-situations.md).

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
- `chances`: per outfit on `outfits`, its selection chance, 0 to 200, 100 when not set
  (`OutfitRows::Chance`; only those not 100 are stored). Set only by the Selection Weight
  slider of the row's Selection Chances in the Edit situation popup (§5), saved as its other
  edits are, once it closes. A new
  outfit on the row has 100; a reorder, random off and on again, and a drag to another
  situation keep it (the target's own value wins when it has the outfit already); an outfit
  that leaves `outfits` loses it. Kept in the co-save's `CHNC` record (design.md §5).

What a row wears:

- Random off: `chosen`. The Default with `chosen` 0: its `pieces`.
- Random on: one of `outfits`, picked when the actor starts wearing the row (the resolved
  row changes, edge-triggered as the poll is) and kept until it changes again; the record
  holds the picked id (`wornOutfit`, since `chosen` cannot tell it). The pick is in
  proportion to the chances (`OutfitRows::Pick`): an outfit's chance over the sum of the
  list's, so 200 is picked twice as often as 100 and 0 never; repeats are possible. A row
  with an outfit above 0 always wears one: the chances are ratios, never a chance of
  nothing. The Default with an empty list, or every outfit at 0: its `pieces`.
- A row *has an outfit* when it wears something by these rules (`OutfitRows::PicksAny`: a
  random row needs an outfit above 0); `Situations::Applicable` is walked as today, the
  first row that has one is worn, else the Default, else their own clothes. A pick worn
  stays on when its chance is set to 0 while other outfits of the row are above it; with
  every one at 0 the row is passed over at the next dressing.
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
- The Dress page's Apply popup: `Apply Only (?)`. The ticks
  become the Default's `pieces`, `chosen` 0, random off; `outfits` stays, hidden. Its help:
  `Sets the selected items as #{Name}'s default outfit without adding them to the Wardrobe.`,
  then, while a situation row holds an outfit, `The new default outfit takes effect when no
  situational outfit applies.`
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

The co-save keeps the rows in the `ASGN` record (version 6, situations by key), the
items left off in `EXCL` and the selection chances in `CHNC`; the layout is in design.md §5.

## 2. Situation data

`Data/SKSE/Plugins/Livery/situations.json`, global like `outfits.json`:

```json
{ "version": 1, "situations": [ { "key": "Town", "name": "City", "removed": false } ] }
```

- `key` is a preset's (`Default`, `Town`, `Castle`, `Home`, `Sleep`, `Cold`) or a user's
  situation's (`user:<n>`), never shown or changed. Version 1, shown here, holds only the
  presets that differ from installed, and is written while nothing else differs
  (`SituationTable::FitsVersion1`); version 2 holds every situation in the window's order
  with its conditions (docs/custom-situations.md §6). A missing or empty file (nothing but
  whitespace) means every preset as installed and is not written at load. An entry's other
  fields are ignored, and a rewrite drops them.
- Presets: name as the key, stored in English; a preset still named so shows in the chosen
  language (`SituationTable::DisplayName`), and that name handed back unchanged is stored as
  the key (`StoredName`).
- Names: never empty; unique among the situations not removed, as stored and as shown, ASCII
  case ignored (`SituationTable::CheckNames`). The
  Default can be renamed but not removed; its name shows wherever a row is named (the
  HUD still adds no ` for <name>` for it).
- The same write rules as `outfits.json` (CLAUDE.md §2.1): `Load` sets the writable state
  once and never writes a file it could not read or that a newer version wrote; writes go
  through a temporary file and a rename; game thread only; only after the user's action
  (§5, §6).
- A removed situation offers no row anywhere: not in an outfit's details, the Situation
  category group, the sort's headings, or the page's
  list; `Situations::Evaluate` still reports it and nothing wears it. Removing it clears
  its rows in this save (§5); another save's rows for it are dropped when that save loads,
  with a log line. A save not loaded between a preset's Remove and Restore Presets keeps its
  rows; a user's situation leaves the file for good.
- `SituationStore::Name` (the name as shown, `DisplayName`) replaces `Situations::Name`
  wherever the window and the HUD show a situation; the log's own lines keep the key.

The sentences that say what is worn end in ` for <name>` (the texts of `LIVERY_STRINGS_HUD_DRESSING`),
nothing for the Default; the summary line names the Default too. Only these:

| Where | Text |
| --- | --- |
| Inventory summary line (`DescribeAssignment`) | `Wearing X for Town.`, `Wearing X as Default.`, each name with its icon in the palette's `Emphasis` colour (`Theme::SubtitleRun`); `Dressed by Livery.` for pieces that are not a saved outfit or an outfit gone from the library; with pieces Livery put on no longer worn (`Request::piecesNotWorn`), `Assigned X as Default, 1 item not worn.`, `Dressed by Livery, 2 items not worn.`; with worn armor Livery did not put on, `, 1 extra item worn` after them (`GapsOf`) |
| Situation change HUD (`SituationChangeText`) | `Livery: Lydia now wears "X" for Town.` |
| Assign HUD, nothing went on | `...; dressed now for Town.` |
| Assign HUD | `Livery: Lydia now wears these for Town` |
| Change HUD, the end | `. Dressed now for Town` |
| Refresh HUD | `Livery: Lydia now wears "X" for Town` |

The names also show in: the Apply popup's `This is Lydia's Town outfit.`, the star's `For Town,
Cold`, the Situation headings, the rows' labels,
Remove's popup (`Lydia: Default, Town`), the Change HUD's `"X" for Town, Cold` and `no
outfit for Town any more`.

## 3. The Situations page

- The third tab of the tab column (docs/outfit-management-ui.md §3): the star, point down,
  that marks a situational outfit in the outfit list, tooltip `Situations`.
- Title `Situational Outfits (#{Name})` after the star, no summary line. Two panes as on the Wardrobe page:
  the Situation List (§4) on the left, the outfit list or an outfit's details (§5) on the
  right. A situation's settings are in its Edit situation popup (§5), opened by its heading's
  Edit.
- The preview shows the Dress page's ticks, or the outfit whose details show (§5).
- Every change on the page is an edit, saved shortly after the last one, once the Edit
  situation popup closes, or as the page is left (§6); the popup's Remove and Restore Presets
  save the edits, then write after their popups.

## 4. Situation List (left pane)

- Label `Situation List (?)` above the list (`Theme::LabelText` + `LabelHelpMark`):
  `Each situation lists the outfits #{Name} can wear in it.` / `The lowest applicable situation
  takes priority. Otherwise, the default outfit is worn.` / `Drag a situation by its grip to
  reorder it.`, a line each, the last only with `Enable custom situations`
  (docs/custom-situations.md §1). (The order: docs/custom-situations.md §8.)
- The Default's heading first, pinned, under it the rule of
  docs/custom-situations.md §7, then one heading per situation not removed in
  `Situations::Order()`, each with a grip that drags it with `Enable custom situations` (§7 there). Each: the star of the Situations tab (none for the Default, whose name starts in its place), a square 4px short of the name's line height in the name's colour, 14px in from the row's left edge, then 6px on the name in the outfit list's row style,
  which leaves the select mark its room; with
  random on, `(n)`, the length of `outfits`, in the small heading style, `ItemInnerSpacing.x`
  after the name; with random on and anything showing under it, a chevron, pointing
  down while open and right while folded, grey, lit while hovered (white in Vanilla and
  Nordic, the accent in Modern); then, 12px on, a rule in the list's rule colour; 12px on,
  with random on, a die (a hexagon split into three faces with one, three and two pips, a
  square 4px short of the name's line height, grey), its square ending 8px short of `Edit`;
  and `Edit` at the pane's right end, in the small heading style, lit as the chevron is. A heading
  without a chevron is never folded: random off shows its one outfit; a heading with nothing
  under it wears nothing and its name is greyed. Every heading is open when the page opens.
  The name and star of the situation worn now (the lowest that applies, or the Default while
  none does) are in the palette's `Positive` colour; those of a situation met now but outranked
  by a lower one, in that colour at half opacity; a heading wearing nothing stays greyed; the
  never-worn error colour wins (`SituationHeadingLook::nameColor`). While the page shows the
  list, the window asks `Request::onCheckSituations` what applies now, at once and every 0.5 s
  once answered (`Assignments::SituationsNow`, the poll's reading; `CaptureWindow::
  SetSituationsNow` takes the latest ask's answer); nothing is coloured before the first answer.
  Hovering the star or the name shows a tooltip (`SituationHeadingLook::nameHovered`): for an
  outranked situation `Met now, but #{Situation} takes priority.` naming the one worn, with its
  star, then a blank line; then the situation's `Applies` text (§5's, each condition met now in
  the `Positive` colour, one with nothing selected greyed, asked for the hovered situation as the
  popup asks for its own; the Default's words without its star). The never-worn mark's tooltip
  (docs/custom-situations.md §7) ends with the same `Applies` text after a blank line.
- Click areas: from the chevron (with its padding) to the die (to Edit with no die), rule
  included, folds; left of it is the name's area (with no chevron, everything left of the die
  or Edit), hovered as a row is but doing nothing when clicked; the die with 4px on each side;
  Edit with its padding to the right end. Edit opens the situation's Edit situation popup
  (§5), the page's edits saved first; its heading is drawn selected (the single-choice row
  highlight of the skin) while the popup is open. A drag from anywhere on a heading still
  moves it in the order (docs/custom-situations.md §7).
- The die's area (random on) is a button while the actor wears an outfit of the row's list
  (as edited) for it and another is on the list: lit as the chevron while hovered, with `One of these
  outfits is picked at random each time this situation begins. Click to pick another now.` on
  hover. A click saves the page's edits
  with it (Saving below), then puts on another outfit of the list, picked by the chances
  (§1), never the one worn or one at 0; an outfit's details close, so the preview shows what they wear. Else a click
  there does nothing, the die stays grey, and hovering it shows the same first sentence, then
  why: `#{Name} is not wearing one of them now.` or `Add another to pick from.`
- Under a heading, its outfits as rows of the outfit list (name, the form marks but no
  star, since each is chosen for a situation), each with a drag grip: two columns of three
  dots, in LabelText's grey at low alpha, here level with the headings' names (`Theme::Grip::kName`), in the right pane's
  outfit list where its fold headings' text starts, after the chevron (`kEdge`); the name
  starts 12px past the dots. Random on: every outfit on the list. Random off: `chosen`
  only. A row that wears nothing shows nothing under its heading; the Default wearing its
  `pieces` shows a `Items not in the Wardrobe` line with no grip; an id the library no longer
  has (another save's) shows nothing (§1).
- While the player dresses from their inventory only (`Request::carriedOnly`), an outfit row
  here, in the right pane's outfit list or as the name row of its details carries a warning mark
  nearest the name (`OutfitMarks::warning`, in the error colour) while one of its items is
  `PieceGroup::kNotCarried`: here only those the row puts on (the items it leaves off out),
  elsewhere any. Its tooltip: `Some items in this outfit aren't in your inventory.` and `With
  "Dress yourself from inventory only" on, they won't be equipped.`, a line each.
- A click on an outfit row, here or in the right pane's outfit list, shows the outfit's
  details in the right pane (§5) and puts it on the preview; a click on the outfit whose
  details show goes back, as Back does. Its rows in both panes show as selected meanwhile
  (docs/outfit-management-ui.md §1): in Vanilla and Nordic the equipped mark sits left of the
  dots here, and in the right pane takes the dots' place, shrunk to leave 2px on either side.
  A click on the pieces line does nothing. A drag never counts as a click.

Drag and drop (`ImGui::BeginDragDropSource` / `Target`):

| From | Dropped on | Result |
| --- | --- | --- |
| an outfit list row (right), or the name row of an outfit's details (§5) | a heading, its rule, its `Items...` line | added at the front (§1) |
| an outfit list row (right), or the name row of an outfit's details | between two outfit rows of a situation | added there |
| a situation's outfit row | another situation, as above | moved: off the source row (its `chosen` 0 if it was that), added to the target with the items it left off there and its selection chance, unless the target has it already; its details, when shown from the source, follow it |
| a situation's outfit row | its own situation | reordered |
| a situation's outfit row, or `Items not in the Wardrobe` | the right pane | taken off that row (`chosen` 0 if it was that; the `pieces` dropped) |

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

## 5. Right pane and the Edit situation popup

It shows the outfit list while a drag from the left pane is on; else the details of the
outfit clicked last, until Back; else the outfit list.

The outfit list: the label `Outfit List (?)`, level with `Situation List (?)`, its help `Drag
an outfit onto a situation to add it, or between outfits to set its position.` / `Drag it to
another situation to move it, or back here to remove it.` / `Click an outfit to see its
items.`, a line each; under it the shared outfit list (docs/outfit-management-ui.md §1) with
its own search and sort, each row with the drag grip, and nothing under the list. Its star and
Default marks (and the star's `For Town, Cold`) follow the page's edits, saved or not, at
once; in the Situation List too, the Default mark does.

An outfit's details, in the Wardrobe page's order (`DrawOutfitSummary`): its name with no
label, as a list of one outfit row drawn as the outfit list's (grip, icon, the worn and Default
marks, no situation star), its name cut short with an ellipsis; the grip drags a copy of the
outfit as the outfit list's rows do (§4: as from the outfit list, so the right pane stays), and a
click on it does nothing. Then `SITUATIONS (#{Name})`, its label starting with the star of the
Situations tab, a square of the label's line height in its grey, and 6px (`Theme::LabelText`),
with the rows the page's edits
give it (`Default, Town`), placed as the Applies text is; neither the label nor the text while
no row holds the outfit. Then its pieces, with the Dress page's icons, under the
`Name` and `Type` labels that sort them as on the Wardrobe page, over their count line. Clicked
under a situation's heading, the pieces are the Wardrobe page's tick rows, ticked as that row wears the
outfit; a tick is an edit of that row (§1 `excluded`), saved as any edit is, and the last
ticked piece cannot be unticked. Clicked on the outfit list, or once that row no longer
holds the outfit, they are read only: no row ticked, none taking a click, drawn ungreyed. The outfit's row in the Situation List is drawn
selected only under the heading it was clicked under. The preview shows the outfit less its
unticked pieces. The room above every button row, then `Open in Wardrobe` (wardrobe icon before its label) and `Back` centred; when they
would leave less than one item spacing on either side (`ItemSpacing.x`), they go one over the
other, as far apart as buttons side by side are. The details grow with the pieces, moving the page's
buttons down when taller than the Situation List, as the Wardrobe page's selection does; the
pieces' list scrolls only to keep the window within 90% of the screen's height, as every
list does (`ListBottom`). Back, and Escape, show
what the right pane showed before and the Dress page's ticks on the preview. Open in Wardrobe goes to
the Wardrobe page with the outfit selected and on the preview, once the page's edits are saved
(§6).

The Edit situation popup, a modal dressed and placed as docs/custom-situations.md §7 says:
the heading `Edit` and the situation's name after its star (the Default's form for the
Default; `Theme::HeadingRunText`), the name as last entered, not while it is typed; no
subtitle. Its left column holds the situation's settings, its fields evenly spaced with no
headings; with `Enable custom situations`, every situation but the Default has a right column,
`Conditions (?)`, with the help of `Applies`, over the condition editor of docs/custom-situations.md §7. Off, and for
the Default, the left column alone. The popup's edits (the name, Random Outfit, the
selection weights, the conditions) are saved once it closes (§6). The left column:

- `Name`, an underlined field (as the Save page's), its label with no icon. Ending the edit with it empty, or with a
  name another situation has (ASCII case ignored), puts the previous name back; with that
  empty too, `Unnamed Situation`. Enter or the field losing focus ends the edit, and the name
  is saved as any edit is; no save starts while it is being typed. Under the Default's, after
  `SectionGap`, `This is #{Name}'s default outfit.` (`Theme::RunText`).
- `Applies`: every situation's but the Default's is the Applies text with its links and the
  label's help of docs/custom-situations.md §7; the Default's, read only and greyed, with no
  help, is two lines `Worn where no ★situation applies,` and `and in
  combat.`, the star the Situations tab's in the text's grey (`Theme::RunText`'s
  `plainNames`), the lines as close as a wrapped text's. The text sits a field's
  frame padding below the label, where a field's text would (`DrawFieldText`).
- `Random Outfit (for #{Name}) (?)`, a checkbox: `On: Each time this situation begins, #{Name}
  wears a random outfit from the list until it ends.` Unticking it keeps the last pick on
  (§1).
- `Selection Chances (?)` while Random Outfit is on and the row has an outfit (`Theme::LabelText`,
  `Theme::LabelHelpMark`; its help `The chance each outfit is picked.` / `Click an outfit to set
  its weight with the slider.` / `An outfit with a weight of 0 is never picked.`): the row's
  outfits in its order as the outfit list's rows, with no grip and no marks; in their place, at
  the row's right end, the chance a new pick is that outfit, its chance over the sum of the
  row's (`OutfitRows::ChanceTotal`, as `Pick` draws), to one decimal place (`7.5%`, `100.0%`,
  `0.0%` with every one at 0). A click on a row draws it selected and opens its slider under
  it, pushing the rows below down, and closes the one open; a click on the open one closes it;
  every one is closed as the popup opens. The slider, 0 to 200 (§1 `chances`), runs from where
  the row's icon starts to where its chance ends, an item spacing above and below it, 8px lower
  than a frame, its grab in the scroll bar's grey (`ScrollbarGrab`, the label colour while
  dragged), `Selection Weight: 100` on it. Moving it edits the row's chance and every row's
  chance shown follows at once. The list scrolls once the popup would pass
  `kMaxWindowHeightRatio` of the screen's height.

Under the columns, the room above every button row (`ButtonRowGap`), then `Done` centred;
Escape is Done unless it closed a dropdown or a popup over the popup.

- `Duplicate Situation` at the left end of Done's row while custom situations are offered,
  not on the Default (docs/custom-situations.md §7).
- `Remove Situation` at the right end of Done's row, drawn as Restore Presets is, without
  the `(?)` (`Theme::LineLink`); not on the Default. It closes the Edit situation popup;
  Remove's popup opens once the edits made in it are saved. Its popup: `Remove Town?`, then under `Used by:` the
  actors in this save with outfits on their Town row's list, hidden ones included
  (`Lydia: 2 outfits`, the length of `outfits`; a FormID when not in memory), for a preset `Restore
  Presets can bring it back, but not their outfits.` (`... can bring it back.` when nobody has any), for a user's situation no such line; `Remove` /
  `Cancel`. The page's edits are saved first, and a refused save opens no popup. Remove writes `situations.json` first; a refused or
  failed write changes nothing and shows the error. Then `Assignments::ForgetSituation`
  clears the row for every actor in this save, and those wearing it are redressed by
  docs/outfit-management-ui.md §6 (the others in memory at close, the rest when they load).
  Nothing is selected.

## 6. Buttons and leaving

- With `Enable custom situations`, `New Situation...` (the star before its label) then `Exit`,
  else `Exit` alone, where the other pages put their buttons; New Situation... saves the
  page's edits, then opens the New situation popup (docs/custom-situations.md §7). At the row's right
  end `Restore Presets (?)`, a link drawn as the Edit situation popup's Remove Situation is, its
  `(?)` inside its click area, its text centred on the buttons' height
  (`Theme::LineHelpLink`).
- Restore Presets: greyed, with `Every preset situation is as installed.`, while they are.
  Its help: `Brings the preset situations back as installed: a removed one returns, empty,
  and a renamed or edited one gets its name and conditions back. Their order and nobody's
  outfits change.` It always asks: `Restore the preset situations?`, then the changes (`Town: restored`,
  `Home: name reset`, `Cold: conditions reset`, `Sleep: name and conditions reset`); `Restore` / `Cancel`. The page's edits are
  saved first, and a refused save opens no popup. Restore writes `situations.json` (a refused
  or failed write changes nothing and shows the error) and keeps the selection.
- Saving: each edit (a drop, Random Outfit, a tick of an outfit's details, a selection
  weight, a name entered, a condition, Allow during combat, a situation moved in the order)
  starts the wait of `kAutosaveDelay` (0.5 s) again, so a burst of edits writes and redresses
  once (`ScheduleAutosave`, `TakeAutosaveDue`). A drag, an open popup (the Edit situation
  popup's edits wait for it to close) or a name being typed holds the save until it ends. The page takes no input while a save runs but keeps its look
  (`BeginPageDisabled`, `State::quietSave`), and the HUD says nothing
  of a rename (the log does); the redress has its usual HUD message. A save writes the global edits (names, conditions, the order; `SituationStore::Apply`, one write) to `situations.json` first, when there are any; a
  refused or failed write changes no row and shows the error. Then, when a row changed, it
  replaces the captured actor's rows at once (`Assignments::SetRows`, so the page shows them
  even before the redress) and dresses them once (`Assignments::Refresh`), by
  docs/outfit-management-ui.md §6 (at once, Live Preview on or off; with it on the preview
  begins again on the outfit whose details show (§5), else on what they wear); a random
  row they wear wears the outfit it gained first. A die's click (§4) saves at once, with
  `SituationSave::reroll`: after the rows, `Assignments::PickAnother` picks another outfit
  of the row they wear and the `Refresh` wears it, even with no edit to save. The page
  stays, an outfit's details too, and the edits start again from what was saved.
- HUD: `Livery: created the situation "X".` (Create, Duplicate), `Livery: removed the
  situation "X".` or `...; cleared from 2 characters.` (Remove), `Livery: restored the preset
  situations.` (Restore Presets).
- Exit closes the window; Escape does what Exit does, first clearing a search. While a write
  runs (a save, Remove, Restore Presets) the page takes no input, Remove and Restore Presets
  greying it, and Escape does nothing,
  here or as an outfit's Back.
- Exit, Escape, another tab or an outfit's Open in Wardrobe with edits not saved yet saves them at once
  and goes once the write took; no alert asks. A refused save stays and shows the error, and
  leaving again with the error showing drops the edits and goes, so a write that keeps failing
  cannot hold the window open.

## 7. Code

- `src/SituationStore.cpp` / `.h`: load, names as shown, removed, the rules (`RulesOf`), the
  situations offered (`Offered`), the `Apply` (names, conditions and the order in one write) /
  `Create` / `Duplicate` / `Remove` / `RestorePresets` writes, Restore's change lines. The
  rules that need no game (the presets, both versions' parse and write, the name rules with
  `DisplayName` / `StoredName`, the order, Restore's change lines) in `src/SituationTable.h`,
  which `test/SituationStoreTests.cpp` reaches.
- `src/Assignments.cpp`: the rows, the co-save record, the
  pick, "has an outfit" and "holds", `ForgetOutfit`'s rules, `ForgetSituation` and the rows
  of removed situations dropped at load. The row rules that need no game (insert at front,
  move, `chosen` on remove, resolve) are in `src/OutfitRows.h`, which
  `test/OutfitRowsTests.cpp` reaches.
- `src/SituationPage.cpp`: the page, its drag and drop, popups and buttons;
  `CaptureWindowInternal.h` holds the page and its state. `CaptureWindow.cpp` has the tab
  column's tabs and what leaving a page does (`RequestLeave`, `FinishLeave`), with the
  Wardrobe page's unsaved-changes alert (`DrawLeavePopup`).
- `src/OutfitRows.h`: `Place`, a drop at a place in a row's list (`Give` is a drop at the
  front); `src/Assignments.cpp`: `SetRows`, `HoldersOf`, `Refresh`'s pick, `PickAnother`.
- `Theme`: the tab icon (the star of `OutfitMarks`), the Situation List heading
  (`SituationHeading` with its `SituationHeadingLook`, the die of random on, a button with
  `diceButton` and a tooltip with `diceTip`, right-hand chevron and Edit with their
  own click areas, `HeadingClick::kEdit`, `kDice`), the
  drag grip (`Theme::Grip` for `OutfitRow`), the
  Edit situation popup's Remove Situation link (`LineLink`), each condition's Remove
  (`LabelLineLink`),
  Restore Presets' (`LineHelpLink`).
- `CaptureWindow::Request`: the situations offered (`SituationInfo`: key, name,
  whether the user's, rules, who has outfits on it) from the store; each row's
  `outfits`, `random`, `chosen` for the captured actor; `onSituationsSave`,
  `onSituationCreate`, `onSituationDuplicate`, `onSituationRemove`, `onRestorePresets`, each
  writing first by §5/§6; `onCheckConditions` and `onCheckSituations`, which write nothing.
- `Plugin.cpp`: the handlers, the redress queue (docs/outfit-management-ui.md §6), the
  summary line.

## 8. Checks

- Rows: save and load again; Remove
  of a chosen outfit with another hidden behind it wears neither; Remove of the Default's
  outfit keeps its pieces on; `Apply Only`; Update of an outfit an NPC wears for a
  situation shows at their next change. Unit tests cover the row rules.
- Situation data: no `situations.json` until something is written; the summary line and the
  HUD read ` for Town`; the summary line `as Default` for the Default.
- The page: each drop of §4's table; the 1 s open; Edit opening the Edit situation popup and
  a click elsewhere on a heading doing nothing; the popup's columns with Enable custom
  situations on and off and for the Default, its Remove, New Situation... opening it on the
  new situation; an outfit's details from
  either pane, with Back, Escape and Open in Wardrobe, the labels' sort, both button layouts, and the preview; several drops in a row redressing
  once, about 0.5 s after the last (Live Preview on and off); Random Outfit on, picks at situation changes;
  Selection Chances in the Edit situation popup only with Random Outfit on and an outfit on the
  row, a row's slider opening and closing, every chance following the slider, the list
  scrolling once the popup reaches its height limit, a 0 never picked and every one at 0
  passing the row over, the value kept across a save and load;
  the die of the row worn lit with its tooltip, a click wearing another outfit (Live Preview on and off),
  the other dice unlit with their reasons;
  rename refused when empty or taken; Remove and its popup, the rows gone; Restore Presets brings it back empty; the
  Exit, Escape and a tab right after an edit, saving before they go; the three skins.
