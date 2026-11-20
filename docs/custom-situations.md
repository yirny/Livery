# User-defined situations

Design, decided 2026-11-13. Built and checked in game: 2a to 2c (§9); 2d (§8, the presets'
rules and the order) is built, not checked in game. It turns the situations of
docs/design.md §7 into one model: the five presets and the situations the user defines, each
a key, a name, a list of conditions and a place in one order. The registry
(`Situations::Count`, `Order`, `AddUser`, `kMaxSituations = 64`) is its base; the model is
`src/SituationRules.h`, the file `src/SituationTable.h`. docs/memo-custom-situations.md keeps the place and spell
decisions in detail and the Skyrim.esm facts behind them; this file is the specification.
Items marked *provisional* are tried in a prototype first and may change.

## 1. Scope

- A user-defined situation has a key (`user:<n>`, §6), a name (the same rules as the presets':
  text, unique among situations not removed, ASCII case ignored), a list of condition rows
  (§2), an `Allow during combat` flag and a place in the order (§8).
- It is chosen for outfits like a preset: a row per assigned actor (`OutfitRows`), the Situations
  page, the HUD and the logs treat it the same; the co-save stores it by key, so no co-save
  version changes.
- The presets are situations like the user's: the same model, their installed rules held
  in `SituationTable::PresetRules` (§3), edited, reordered and removed through the window as
  the user's are. The one difference: Restore Presets brings a preset back as installed; a
  removed user's situation is gone. The Default is neither: no rules, no place in the order.
- The settings page's `Enable custom situations` (`[Window] CustomSituations`, default off,
  under the `Optional Features` heading with `Enable chests`; `Settings::CustomSituations`, read as the
  window opens, `Request::customSituations`) offers the editing: New Situation..., Duplicate Situation, the order's
  drag and the condition editor (the Edit situation popup's right column and the Applies text's
  links, §7). Off, every
  situation made stays in use, and the Situations page still lists them with their outfits,
  their Applies text read only, the never-worn mark, Remove Situation
  and Restore Presets; renaming stays too. The poll does not read the setting.
- The first version ships the kinds of §3; §4 lists the kinds held back.

## 2. Condition model

A situation holds a list of condition rows. The situation applies while every row holds
(AND). A row is a kind, a `not` flag, for a kind with values a set of values, and for a kind
that reads an actor's own state (`SituationRules::Targetable`: In Combat, Sleeping, Swimming,
Spell Effect, Weapon) whose state it reads (`Row::target`): the character wearing the
situation's outfit, or the player whoever wears it (a follower who changes when you fight). A
row with values holds while any of its values holds (OR), and `not` inverts the row. Rows do not
nest:
there are no OR groups (*provisional*: revisited after use; so far every wanted rule
fits, "night or rain" being two situations). The same kind may appear in several rows
("Rain" and "not Freezing region"). A row with values and none selected never holds, so its
situation never applies; the Situations page says so (§7). A situation with no rows never
applies either.

Combat: every situation but the Default has the flag `Allow during combat`:
off, the situation does not apply in combat, so a fight is fought in the Default unless another
one applies (the wearer's combat, whoever the rows read); on, its rows decide. A situation with
an `In Combat` row, reading the wearer or the player, has the flag on and not editable: the row
would never hold otherwise, or only while the player fights and the wearer does not. Every
preset has it off as installed.

## 3. Condition kinds

Each row costs the poll one read per actor and situation; none allocates or logs. A value
whose plugin is not loaded never holds. "Verified" means the rule runs in Livery today.

| Kind | Values | Holds while | Source | Verified |
|---|---|---|---|---|
| In Combat | none | the actor is in combat | `Actor::IsInCombat` | yes (as today's rule) |
| Sleeping | none | the sleeping rule of docs/design.md §7 | `GetSitSleepState`, CanSleep furniture | not yet |
| Swimming (key `water`) | one of In water (key `inWater`), Swimming | In water = the in-water rule: swimming, or water no lower than 40 units under their feet; once in, out only past 60 units; on a mount swimming only. Swimming = the actor state's swimming flag | `IsSwimming`, `GetWaterHeight` | In water by wading yes; Swimming not yet |
| Indoors | none | their cell is an interior cell | `TESObjectCELL::IsInteriorCell` | no |
| Location type | Town, Castle, Player home, Dungeon | their current location's own keywords: Town = `LocTypeCity`, `LocTypeTown`, `LocTypeSettlement`, `LocTypeDwelling`, `LocTypeInn`, `LocTypeHouse` or `LocTypePlayerHouse`, for every actor, Castle = `LocTypeCastle`, Player home = `LocTypePlayerHouse`, Dungeon = `LocTypeDungeon` | `BGSLocation::HasKeywordString` | Town yes; Castle, the rest not yet |
| Place (Specific Location in the editor) | a list of locations and cells | the memo's place rule: their current location or any of its parents (`parentLoc`), or their cell, is in the list. The open country belongs to no place: most of its cells have no location (Skyrim.esm: 930 of Tamriel's 11,187 exterior cells have one, one of them a hold's), so `GetCurrentLocation` is null there | `GetCurrentLocation`, `GetParentCell` | yes (a home, a city; the open country checked null) |
| Weather | Rain, Snow, Warm region, Cool region, Freezing region (keys `rain`, `snow`, `warmRegion`, `coolRegion`, `freezingRegion`) | read around the player, whoever wears the situation. Rain = the player is in an exterior cell and the deciding weather has `kRainy`, Snow = `kSnow` (while the sky blends, the last weather decides until the new one is halfway in). A region = the climate of the player's place (`RegionClimate`): outdoors Survival Mode's classes by the cell's regions and the player's height (the conditions of its `Survival_RegionInfoSpell`, in its order: the two mountain areas, warm, cool, then freezing), Windhelm's city region freezing too; a place it does not class (a mod's land) Freezing when a region of its cell has snow weathers for at least 40% of its weather chance, Cool otherwise; indoors (an interior cell, or one of its interior worldspaces such as Blackreach) none, but its ice caves (by location or cell) Freezing; its Oblivion planes (Sovngarde, the Soul Cairn, Apocrypha) none. The place is read again only as the player's cell or location changes | `Sky::currentWeather`, `lastWeather`, `currentWeatherPct`; `TESObjectCELL::GetRegionList`, `GetPositionZ`, `GetWorldspace`, `GetCurrentLocation` | Rain and Snow by the sky yes, outdoors only not yet; the climate matched Survival Mode's own answer in 17 cells (2026-11-18); the region rows not yet |
| Time of day | From, To (game hours, minutes allowed) | the game clock is in the range; a range whose To is before its From wraps midnight; default 20:00 to 6:00 | `Calendar::GetHour` | no |
| Spell Effect (key `spell`) | a list of `MagicItem`s | a visible effect on the actor, neither inactive nor dispelled, comes from an item whose visible effects are exactly those of one of them | `MagicTarget::VisitEffects`, `ActiveEffect::spell`, `ActiveEffect::flags`, `MagicItem::effects` | no |
| Weapon | none | a weapon, spell or fists drawn, or being put away (`kDrawn`, `kWantToSheathe`, `kSheathing`); negated, sheathed | `ActorState::IsWeaponDrawn` | no |

"The actor" above is the wearer, or with `Row::target` the player (§2); the player's facts are
read once per evaluation, as the wearer's are, and only when a row asks. The player's in-water
record (`g_inWater`) is kept across polls whether or not they have a situation outfit.

Notes:

- The presets as installed (`SituationTable::PresetRules`), each with `Allow during combat` off:
  Town = Location type {Town}; Castle = Location type {Castle}; Home = Location type
  {Player home}; Cold = Weather {Snow, Freezing region}; Sleep = Sleeping.
  Two changes from the first release's fixed rules, decided 2026-11-14 since a rule cannot
  depend on who the actor is: Home no longer holds in any house for an NPC (`LocTypeHouse`),
  and Town holds in any house for the player too. Cold changed too (decided 2026-11-18): rain
  no longer counts, and a freezing region follows Survival Mode's classes instead of the sky
  region's snow. Town and Cold by weather, verified through the fixed rules, are checked
  again through the model (§10).
- In water keeps its hysteresis in a per-actor list of those last told in water
  (`g_inWater`, changed only as someone goes in or out). Swimming needs none: the flag is the
  engine's. In water takes swimming in, so a row with both (only from the file) reads In water,
  and the never-worn mark (§7) counts Swimming as In water (`Detail::Implies`).
- Indoors is the cell's kind. Worldspaces that are underground or elsewhere (Blackreach,
  Sovngarde, the Soul Cairn) count as outdoors; a known quirk, documented, not worked around.
- Location type reads the location's own keywords only, as the presets do (a room of a city is
  not in it by its parent); Place walks the parents. A hold is not offered as a Place, nor
  Tamriel above the holds (decided 2026-11-13): it would hold in the hold's towns, farms,
  camps and dungeons only, never on the roads between them, which is not what its name
  promises; a condition on the cell's regions (`TESObjectCELL::GetRegionList`, as Weather's
  climates read them; regions have editor IDs only, `WeatherTundra`) would cover the open
  country, if wanted.
- Weather reads around the player, not the wearer: the sky is theirs, and the actors the poll
  dresses are near them, so all change together. Its indoors is Survival Mode's, which counts
  worldspaces such as Blackreach as indoors, unlike Indoors.
- Time of day: game time, so waiting and sleeping jump the clock; the poll sees the new hour at
  its next run. From equal to To is the whole day.
- Spell Effect: the row lists spells, powers, shouts (by their words' spells), potions,
  scrolls, ingredients and abilities (`MagicItem`), by the names the player knows, not magic
  effects (the memo's reasons), and holds by their effects. A visible effect is one the game's
  menus show: named and not `kHideInUI`. Each listed spell's visible effects are resolved as
  the store reads or saves the rules (`Situations::VisibleEffectsOf`, kept in
  `SituationRules::FormRef::effects`); an effect on the actor counts when it is visible itself
  and its item's visible effects are exactly that set (`SituationRules::SameEffects`), so
  Stoneflesh's scroll counts for Stoneflesh and a potion made in game for the potion with its
  effects. Exactly, not any shared one: hidden effects are shared widely (every flesh spell
  carries the hidden `PerkArmorFFSelf30/50/70`), and visible ones too (all food shares one,
  the shrine blessings share Cure Disease). A spell without visible effects counts by its own
  FormID. The walk is the engine's (`MagicTarget::VisitEffects`, which CommonLibSSE-NG names
  the one walk right on every runtime); it skips effects flagged `kInactive` or `kDispelled`,
  as an effect that does not act is not on the actor (whether the engine leaves such effects in
  the list: §10). Per effect it compares the item's few effects against a sorted set: no
  allocation, no lookup.

## 4. Pending kinds

Not offered in the first version; added on request. The rule each would use:

- Sitting: `GetSitSleepState() == kIsSitting` and not `IsOnMount()`; `kRidingMount` has the
  same value as `kIsSitting`.
- Mounting: `Actor::IsOnMount`.
- Sneaking: `Actor::IsSneaking`.
- Location type values House (`LocTypeHouse`), Inn (`LocTypeInn`), Dwelling (`LocTypeDwelling`),
  Guild (`LocTypeGuild`).
- Weather values Clear (`kPleasant`), Cloudy (`kCloudy`), Storm (a weather with `kRainy` and
  lightning or a storm sound; the rule is to be decided).
- Faction, Worn item, Actor keyword: from the memo's open list; each is a FormID comparison.

## 5. Evaluation

`Situations::Evaluate(actor, rows)` walks `Situations::Order()` from its end, so the lowest
situation in the window's list is told first (§8), and adds each that applies. It reads only
the situations whose row in the actor's rows holds an outfit (`Assignments::SituationsNow`
passes them): one they have no outfit for cannot be worn, so `OutfitRows::Resolve` gives the
same row, and with none it returns before any engine read. Every situation, preset or the
user's, is its rules (`SituationStore::RulesOf`, null for the Default alone): in combat, only
with `Allow during combat` on; then every row must hold.
What a row reads of the actor (the location, in water) is read once per evaluation. A row with a
list of forms compares FormIDs resolved as the store reads or saves the rules
(`PieceFile::FormIDOf`) and kept with them (`SituationRules::FormRef::formID`), so the poll
compares integers and walks parents (at most 32, against a loop in a mod's data); no name lookups, no allocation,
no logging. The poll, its one-redress-per-run rule and the 3 s hold are unchanged.

The registry is mutable: `SituationStore::Load` adds the file's user situations at
`kDataLoaded`, before any co-save load, `SituationStore::Create` adds one after its write
took and `SituationStore::Apply` reorders them (`Situations::Reorder`), all on the game thread; the window is open then, so the poll does not run. Each list is
reserved for `kMaxSituations` (64, the Default and the presets included), so adding moves
nothing. A removed situation stays in the registry and the store's table until the game is
closed, so no id moves or is reused; it counts toward the 64 until then, and Create refuses
past them. `OutfitRows::Rows` reads empty past what it stores, so a new situation needs no
row change.

## 6. Data

situations.json is written as version 1 while version 1 holds it all
(`SituationTable::FitsVersion1`): no user-defined situation ever made (`nextKey` 1), every
preset's rules and `Allow during combat` as installed, the installed order; so a Livery without
user situations still reads it. Anything else writes version 2; once a user's situation was
made, `nextKey` keeps it there. The parser reads version 1 (presets' names and removed flags
only) as version 2 with the presets as installed, in the installed order. The array is the
window's list, top to bottom: every preset, removed or not, and every user's situation not
removed, each with every field; the Default only when renamed, and never in the order. A
version 2 file missing a preset gets it first in the order, as installed, in the installed
order (`Detail::AddMissingPresets`): a hand-edited file still loads, and a file written before
the presets had rules (2a to 2c wrote only the presets renamed or removed) keeps its own
situations winning over the presets, as they did then. A preset entry without `conditions`
keeps its installed rules. Shape:

```json
{
  "version": 2,
  "nextKey": 3,
  "situations": [
    {
      "key": "Town",
      "name": "City",
      "removed": false,
      "applyInCombat": false,
      "conditions": [{ "kind": "locationType", "not": false, "values": ["town"] }]
    },
    { "key": "Castle", "name": "Castle", "removed": false, "applyInCombat": false, "conditions": [{ "kind": "locationType", "not": false, "values": ["castle"] }] },
    { "key": "Home", "name": "Home", "removed": false, "applyInCombat": false, "conditions": [{ "kind": "locationType", "not": false, "values": ["playerHome"] }] },
    {
      "key": "Cold",
      "name": "Cold",
      "removed": true,
      "applyInCombat": false,
      "conditions": [{ "kind": "weather", "not": false, "values": ["snow", "freezingRegion"] }]
    },
    { "key": "Sleep", "name": "Sleep", "removed": false, "applyInCombat": false, "conditions": [{ "kind": "sleeping", "not": false }] },
    {
      "key": "user:2",
      "name": "Rainy town",
      "applyInCombat": false,
      "conditions": [
        { "kind": "locationType", "not": false, "values": ["town", "castle"] },
        { "kind": "weather", "not": false, "values": ["rain", "snow"] },
        { "kind": "indoors", "not": true },
        { "kind": "place", "not": false, "values": [{ "plugin": "Skyrim.esm", "formId": "0x01A26F", "name": "Whiterun" }] },
        { "kind": "timeOfDay", "not": false, "from": "20:00", "to": "06:00" },
        { "kind": "spell", "not": false, "values": [{ "plugin": "Skyrim.esm", "formId": "0x059B7A", "name": "Stoneflesh" }] }
      ]
    }
  ]
}
```

- `key`: `user:` and a counter from `nextKey`, taken once and never reused; a rename changes
  the name only. It cannot collide with a preset key or a retired one (`Warm`, `Swimming`).
  `Situations::FromKey` finds it in the registry. A removed user-defined situation leaves the
  file; another save whose co-save still has rows under its key reads them as it reads a
  retired key today: dropped. While `situations.json` cannot be read, the co-save keeps the
  rows of every user's key instead and writes them back (docs/design.md §5).
- Form values are stored as a piece is (`PieceFile::PieceRef`: plugin file name, local FormID,
  the name for the reader); a value whose plugin is not loaded stays in the file and never
  holds.
- The parser refuses unknown kinds, values and keys as it refuses unknown versions today; the
  file is then read-only for the session (`SituationStore::Load` sets the writable state once).
- The same write rules as every shared file (CLAUDE.md §2.1): the write comes first, through a
  temporary file; a refused or failed write changes no row.

## 7. Situations page

Built in 2a to 2c (`src/SituationPage.cpp`, Condition editor); 2d adds the order (§8).

- The Situation List: the Default's heading first, pinned, then the caption `The lowest that
  applies is worn.` as a list heading (`Theme::ListHeading`) with a plain 1px rule after it in
  every skin, so it does not read as a situation's heading, then one
  heading per situation not removed in the order (`SituationForm::order`, request.situations'
  until a drag), each dragged from anywhere on its heading (`kSituationPayload`, apart from the
  outfits') to another place among them, with `Request::customSituations`: over a heading's
  upper half before it, anywhere lower in its block after it, on the caption first; the drop
  shows as a line, as an outfit's does between outfits, and a see-through copy of the heading
  follows the mouse (`Theme::SituationDragGhost`), as an outfit row's does. While the heading
  is hovered, grip dots take the star's place, where the rows' dots under it are
  (`Theme::SituationHeadingLook::grip`); a click that became a drag does nothing. The label's
  help says so (docs/situation-management-ui.md §4).
- With `Request::customSituations`, the page's button row gains `New Situation...` (before
  Exit): the `New situation` popup asks
  for a name (Create / Cancel, as the New chest popup), saving the page's edits first; the new
  situation, with no conditions, is appended last, where it wins over every other, the
  Situation List scrolls to it and its Edit situation popup opens. Every situation but the
  Default has Remove Situation in that popup (a preset's Remove popup with the Restore Presets line, a
  user's without it).
- With `Request::customSituations`, every situation but the Default has `Duplicate Situation` at the left
  end of the Edit situation popup's Done row (`Theme::LeftLineLink`). It closes the popup, saves
  its edits, then makes a user's situation with the source's rules (conditions and Allow during
  combat) and no outfit, right after the source in the order, so the copy wins over it until its
  conditions change (`SituationStore::Duplicate`). Its name follows Windows: `<name> - Copy`, else
  `- Copy 2`, `- Copy 3`, ... (`ExampleMod::CopyName`, ASCII case ignored), the name cut short at
  a character's start to fit a name field. The HUD and the page then act as for New Situation:
  the Situation List scrolls to the copy and its Edit situation popup opens.
- The Edit situation popup's left column (docs/situation-management-ui.md §5), for every
  situation but the Default, shows the `Applies` label, its help `Applies while every
  condition is met.` and `A condition with several values is met by any of them.`, over the
  Applies text (below), each condition's phrase a link (`Theme::RunLinks`). A condition met
  now is in the palette's `Positive` colour (`#B7DA7B` in every shipped palette); a hovered
  phrase's tooltip says `Met now.` or `Not met now.` With `Request::customSituations` the rest
  is in the text colour, a hovered phrase is lit and underlined, and a click scrolls the editor
  to that condition and fills its box in a selected row's colour, fading out over 1.5 s. Off,
  the rest is greyed and a hovered phrase also says `Turn on custom situations in the settings
  to edit.` after a blank line. The links are for the mouse alone.
- Met now: while the Edit situation popup is open on a situation with conditions, the window
  hands its rows as edited to `Request::onCheckConditions` in an SKSE task, at once when they
  differ from those last asked and every 0.5 s once the last ask was answered;
  `Situations::RowsHold` reads each row alone for the actor now, as the poll reads it, in
  combat or not, and `CaptureWindow::SetConditionsHeld` hands the answer back, taken for the
  latest ask only. Until the first answer no condition is coloured. `Allow during combat` is no
  row and has no colour. Nothing runs while the popup is closed.
- The `Edit situation` popup (docs/situation-management-ui.md §5), a modal (every modal of the
  window: the main window's padding, Vanilla's frame and Nordic's ornaments over the popup's
  own background, moved by a drag on it as the main window is, never with ImGui's own scroll
  bar; the windows under the top one, the main window and a modal it opened over, drawn at
  half opacity over ImGui's dark backdrop, `FadeBehindModal`), centred on the screen at every
  frame, so it stays centred as conditions come and go, until the user drags it
  (`BeginModal`'s `a_centred`, again on the next opening). With `Request::customSituations`,
  for every situation but the Default, it has two columns with the pages' gap and rule
  (`BeginColumns` with a first column's width): the situation's settings on the left, as wide
  as the page's right pane, and on the right, as wide as the widest row any kind can have
  (`EditorContentWidth`), so only a Place or Spell row's list goes under its row, `Conditions`
  (with the `Applies` help, `kConditionsHelp`) over the editor in an area as tall as it while
  the whole popup stays within 90% of the screen's height (`kMaxWindowHeightRatio`; the rest
  of the popup measured the frame before), scrolling past that with the skin's scroll bar in
  the window's right padding, the boxes as wide with the bar as without (`Theme::BeginScrollArea`). Off, and for the Default, the left
  column alone, as wide as the right pane. The Add location and Add spell popups open over it.
  It stays hidden a frame longer than ImGui's own, so its first opening is centred at its full
  height.
- The editor: each condition is in a box, in every skin a 1px line of the rule colour with 4px
  round corners, 12x8px inside and 8px apart. Its heading is the kind's name as a field label
  (`Theme::LabelText`, `KindInfo::label`) with a (?) carrying the kind's help and a `Remove`
  link (`Theme::LabelLineLink`; a word, not the forms' ×) at the line's right end, its capitals
  level with the label's. Under it the
  row reads as a sentence. No `Not` checkbox: the row's `not` is a dropdown of two choices. A
  kind that reads an actor's state (`Targetable`) starts with whose, a dropdown of the actor's
  name and `Player` (`Row::target`; its help `<name>: checks this character` and `Player: checks
  you`; in the player's own window, `Request::actorIsPlayer`, `<name>: checks each character
  wearing the outfit` and `Player: always checks you`); the others with their subject in the
  text colour (`KindInfo::subject`): `<name> is`, `The location type`, `The location`, `The
  weather`, `The time is`. A two-state kind (`SituationRules::TwoState`) then reads `is` / `is
  not` and its phrase, `[Lydia] [is] sleeping`, or is a dropdown of its two states where it has
  them (`KindInfo::state`, `negState`): `Indoors` / `Outdoors`, `Weapon drawn` / `Weapon
  sheathed`. The others have the operator dropdown, as wide as its widest wording so ticking
  more values never widens it: `is` / `is not` with up to one value or form selected, `is any
  of` / `is none of` with more (as Linear and Airtable word it), Spell Effect `has` / `has not`,
  `has any of` / `has none of`, `between` / `not between` for Time of day; then the values.
  Location type's and Weather's are a multi-choice dropdown (the Filter field's rows, staying
  open for several ticks; a value with a tip, `Value::tip`, says it while hovered: Freezing region's
  `Snowy lands, high mountains, the northern coast and ice caves.`) whose preview lists the ticked values, `Rain,
  Snow`, or a greyed `Select...` with none; Weather's has a rule between Rain / Snow and the
  regions (`Value::ruleAbove`), the weather now apart from the climate; Swimming's a single choice (`SituationRules::OneValue`,
  `[Lydia] [is] [In water]`, the operator `is` / `is not` alone), closed by it, its tips `Any
  water underfoot, however shallow.` and `Swimming only, not wading.`; Time of day's are
  `[time] and [time]`, two dropdowns
  in 30-minute steps (a time read from the file off those steps shows as it is). Both sit on the
  row while they fit, else under it, 16px in. Place (`Specific Location` in the editor) and
  Spell list their forms under the row, 16px in, one a line with a × at its right end (`Remove
  this location.` / `Remove this spell.`; one whose plugin is not loaded greyed, saying so on
  hover), then `Add Location...` or `Add Spell...` at the list's left. In Vanilla and Nordic a
  row's dropdowns have the palette's stripe colour behind them, the frame padding at both ends (`FieldStyle::filled`). Then `Add Condition...`, a dropdown with the Add links' plus mark before its text (`FieldIcon::kAdd`), of the
  kinds by label, closed by the choice, which adds a row with nothing selected, Swimming's with
  In water (a Place or Spell row opens its Add popup at once); a two-state kind the situation has every row it can take of
  (one; one per target for a kind with a target) is greyed there, a second adding nothing.
- `Add Location...` opens the `Add location` popup, its choices read as the window opens
  (`Situations::PlacesAround`): `This Location (<name>)`, the actor's current location; a
  `Parent Location (<name>)` for each location it is in, nearest first, up to the first hold
  or location holding a hold (Tamriel), left out with all above it, and of two in a row with
  the same name only the wider (`SituationRules::MergeSameNames`: Solitude's Avenues, a group of
  its houses without its streets, is named "Solitude" as the city is); and `This Cell (<name>)`,
  their cell (a cell without a name shows its editor ID, else its FormID); each followed by a
  (?) saying what it covers. The game's own terms, as "room" or "area" would blur the two. It
  opens on the location, or on the cell when they are outside any. Add is greyed for a place
  the row has already.
- `Add Spell...` opens the `Add spell` popup, its width set before it begins (18 font sizes,
  or the first line's or the checkboxes' row's when wider), since its list runs into the window's
  padding in Vanilla and Nordic and a popup fitted to its content would grow by that at every frame:
  `Add a spell to look for on <name>.` (`on you.` for a row whose target is `Player` in another
  actor's window), a search field (`Search by name`, ASCII case ignored, as the outfit search),
  `Show all spells` and, right of it in another actor's window, `Show player spells` (greyed
  while Show all spells is ticked; both unticked at each open), and a
  list ten rows tall of the spells whose name holds the search, a click picking one. A spell
  there or on the row follows its icon, SkyUI's (design.md §9, Magic icons); a spell of the row
  that no list has, or whose plugin is not loaded, has the icon's room and no icon. Hovering a
  spell there or on the row shows its plugin and FormID, then its visible effects a line each,
  `- <name> (<editor ID>)`, the editor ID only when the game keeps one (it does not by itself;
  a mod such as powerofthree's Tweaks loads them). Both off, the list is the actor's
  (`Situations::SpellsAround`, read as the window opens); with Show player spells, the actor's
  and the player's together, by name, merged as below (read as the window opens too). An actor's
  spells are the spells, powers and shouts they
  cast, learned (`addedSpells`) or of their NPC record and race (`actorEffects`, a shout by its
  words' spells), that can leave a visible effect on someone: one delivered to themselves, or
  one for others whose visible effects are all neither hostile nor detrimental and none of an
  archetype that stays on the caster or lands on no living target (summon, reanimate, stagger,
  telekinesis, command summoned, banish, werewolf feed, spawn hazard, spawn scripted ref, grab
  actor). Of the spell tomes of Skyrim and its DLCs, that adds Calm, Courage, Heal Other, Heal
  Undead, Healing Hands, Magelight (its light follows the actor it hits, as an effect on them),
  Necromantic Healing, Pacify and Rally; Flames, Fury, Turn Undead or a Conjure stay out (diseases,
  poisons and enemies' spells come through `Show all spells` or while they act). Then the items
  behind the visible effects on them now (an ability, a disease
  or a potion only while it acts). Show all spells on, every `SpellItem`, `AlchemyItem`, `ScrollItem` and
  `IngredientItem` of the loaded plugins (`Situations::AllSpells`, built at the first open and
  kept until the game closes), listed only once something is typed (`Type to search.`). Both
  leave out what has no name or no visible effect, enchantments, and forms made in game; both
  are by name, and of spells with the same name and visible effects keep the lowest FormID
  (`SituationRules::MergeSameSpells`: Stoneflesh is in Skyrim.esm twice). Spells with the same
  name and other effects stay apart (decided 2026-11-14): Skyrim.esm's eleven "Flames" are five
  by their effects (the player's, the traps', a creature's, a cloak's, J'zargo's), each a
  different effect named "Flames", told apart by the FormID line alone without editor IDs;
  reading the plugins' editor IDs into Livery was judged not worth its load cost for this
  hover. The first 100 matches are listed, then `<n> more. Type more to narrow.`. Add is greyed with none picked or
  one the row has already; Escape clears the search, or with none typed is Cancel.
- `Allow during combat` is a checkbox a field's gap under `Add Condition...`, apart from the
  conditions since it is not one; its tooltip `In combat, the default outfit is normally
  worn.` and `On, this situation can apply in combat too.` With an In Combat row (not
  negated) it is on and disabled, `On while the situation has an In combat condition.`; a
  preset's is off as installed.
- The `Applies` line, under its own label, is every situation's but the Default's
  (`ConditionsText` stays for the Default alone) and is built from the rows in order, one phrase per row, `not` first: `In a town
  or a castle, in rain or snow, outdoors, at Whiterun, from 8 PM to 6 AM, while Stoneflesh
  is on, with a weapon drawn.` Negated, `not in combat`, `outdoors`, `with weapons sheathed`
  (`KindInfo::negPhrase`); reading the player, `while you are in combat`, `while you are not
  sleeping`, `while Stoneflesh is on you`, `while your weapon is drawn`, `while your weapons are
  sheathed` (`youPhrase`, `youNegPhrase`). A row with nothing selected reads `Weather (nothing
  selected)`; a situation without rows `Never applies.`
- The editor's edits save once its popup closes (the page's autosave waits while a popup is
  open) or as the page is left, the file first. Nobody is redressed for changed conditions then: the poll, which waits
  while the window is open, follows them once it closes.
- While a situation is never worn (§8a), its Edit situation popup says so under the name, after a field's
  gap, in the error colour, each sentence wrapped (`Theme::RunTextWrapped`): `Never worn:
  <name>, lower in the list, applies whenever this does.` / `To change that, add a condition,
  edit <name>, or move one of them in the list.`, naming the nearest after its star, both in the
  emphasis. Its heading carries a warning mark after the name (and its count), a circle with an
  exclamation mark in the error colour (`Theme::SituationHeadingLook::warning`), the same
  sentences its tooltip, in the text colour. While its Edit situation popup is open or its mark hovered, the
  Situation List draws the name of every situation lower in the list that applies whenever it
  does in the error colour (`SituationForm::neverWornBy`, `Theme::SituationHeadingLook::alert`).
- The other way round, the Edit situation popup of a situation that some situation higher in
  the list is never worn for says `Always overrides <name>.` for each of them, one line each in
  the list's order, under the name and after a never-worn text, in the error colour, the name
  after its star in the emphasis.

## 8. Priority

Decided 2026-11-14. One order for every situation but the Default, presets and the user's
alike: the window's list, where the lowest situation that applies is worn
(`Situations::Order()`, which replaces `Choosable` and `Priority`). The Default sits above them
as the base every situation overrides, pinned, and a new situation is appended last, so it wins
over the presets as made. The reading is a load order's: lower wins. Installed order, top to
bottom: Town, Castle, Home, Cold, Sleep (the first release's Sleep, Cold, Home, Castle, Town
reversed). Restore Presets does not touch the order. The order is saved as the page's other
edits are (§7, the file first); nobody is redressed for it then, the poll follows it once the
window closes.

### 8a. Never worn

A situation is marked never worn when one lower in the list applies whenever it does, as far as
the rules tell (`SituationRules::CoveredBy`, computed as the page opens and after each edit,
never on the poll; at most 64 × 64 situations). Situation B, lower than A, covers A when B has
rows, B's `Allow during combat` is on or A's off, and every row of B is implied by a row of A:

- the same kind, `not` and target, and A's values within B's (B's within A's with `not`): the
  value set, the time range, the list of places or spells;
- the same kind and `not`, Place, neither negated: each of A's places is one of B's or within
  one (its parents, as the rule walks them);
- Location type in B by Place in A, neither negated: each of A's places is a location, or a
  cell with one, whose own keywords give one of B's values, and so does every location within
  it.

A place's keywords, parents and the locations within it are read as the window opens
(`Situations::PlaceFacts`, `Request::placeFacts`: the places of every situation's rules and those
the Add place popup offers; one pass over the loaded locations, only when one of them is a
location); a place whose plugin is not loaded gives none. The page works the mark out
(`SituationForm::neverWornBy`, the nearest covering situation below) before it draws after the
form was filled, a condition edited or the order changed. A situation that never applies (no rows, or a row with nothing selected) is
not marked, as its Applies line says so, and covers nothing. Nothing else is told, so a missing
mark proves nothing; the tooltip names what was found. The mark does not read the actor's rows
(whether they have an outfit for B): the file is every save's; revisited after use.

## 9. Implementation order

1. 2a: the condition model, situations.json version 2, the mutable registry, the editor, and
   the kinds that need no form picker: In Combat, Sleeping, Swimming, Indoors,
   Location type, Weather, Time of day. Usable on its own; the prototype of §7 is checked here.
   Built on `feat/cs-model`, checked in game.
2. 2b: Place, with its list line and the add-current-place popup (the first form picker).
   Built on `feat/cs-place`, checked in game.
3. 2c: Spell, reusing 2b's list line, with the search picker and the active-effect walk.
   Built on `feat/cs-spell`, checked in game.
4. 2d: the order and the presets' rules (§8): one list, the presets' rules in the model, the
   grip drag, Restore Presets with rules, the never-worn mark, `Enable custom situations` (§1).
   On `feat/cs-priority`, built.

## 10. Open and in-game checks

- Whether the engine keeps `kInactive` or `kDispelled` effects in the list the walk visits
  (the Spell Effect rule skips them either way), and whether a cast flesh spell holds for its
  whole duration and ends with it (checked in game for Stoneflesh by its FormID; again by its
  effects, with its scroll).
- Which Skyrim.esm locations carry `LocTypeDungeon`, and whether the DLC dungeons do.
- What `Sky::currentWeather` holds in an interior cell (the last exterior weather is assumed).
- A Time of day range that wraps midnight, at the wrap.
- Whether an NPC's parent cell follows them across exterior cells (an exterior `This Cell` place).
- The climates beyond the 17 cells checked: Solstheim, the Forgotten Vale, a height of 19000
  alone, an ice cave; whether 1.5.97's Update.esm has SurvivalForelhost and
  SurvivalArcwindPoint (without them Forelhost and Arcwind Point stay warm up to 19000).
- OR groups (§2), after use.
- Town, and Cold by snow and by region, through the model's rules, now that the presets' fixed
  code is gone.
