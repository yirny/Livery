# User-defined situations

Design, decided 2026-11-13. Built: 2a (§9), checked in game; 2b on `feat/cs-place`, in-game
check pending; 2c is not. It extends the situations of docs/design.md §7 (five presets with fixed rules,
`Situations::Priority()`) with situations the user defines: a key, a name, a list of
conditions, a place in the priority order. The registry (`Situations::Count`, `Choosable`,
`Priority`, `AddUser`, `kMaxSituations = 64`) is its base; the model is `src/SituationRules.h`,
the file `src/SituationTable.h`. docs/memo-custom-situations.md keeps the place and spell
decisions in detail and the Skyrim.esm facts behind them; this file is the specification.
Items marked *provisional* are tried in a prototype first and may change.

## 1. Scope

- A user-defined situation has a key (`user:<n>`, §6), a name (the same rules as the presets':
  text, unique among situations not removed, ASCII case ignored), a list of condition rows
  (§2), an `Apply in combat` flag and a place in `Priority()` (§8, open).
- It is chosen for outfits like a preset: a row per assigned actor (`OutfitRows`), the Situations
  page, the HUD and the logs treat it the same; the co-save stores it by key, so no co-save
  version changes.
- The presets keep their fixed rules in the first version: their conditions are not edited
  through the window. Every preset rule can be written in the condition model (§3), so they may
  become editable later without a second model.
- The first version ships the kinds of §3; §4 lists the kinds held back.

## 2. Condition model

A situation holds a list of condition rows. The situation applies while every row holds
(AND). A row is a kind, a `not` flag and, for a kind with values, a set of values; a row with
values holds while any of its values holds (OR), and `not` inverts the row. Rows do not nest:
there are no OR groups (*provisional*: revisited after the prototype; so far every wanted rule
fits, "night or rain" being two situations). The same kind may appear in several rows
("Rain" and "not Snowy region"). A row with values and none selected never holds, so its
situation never applies; the Situations page says so (§7). A situation with no rows never
applies either.

Combat: `Situations::Evaluate` returns nothing in combat today, so a fight is fought in the
Default. Per user-defined situation, the flag `Apply in combat` (wording provisional) lifts that
for it alone: off, the situation does not apply in combat; on, its rows decide. A situation with
an `In Combat` row has the flag on and not editable, since the row would never hold otherwise.
The presets keep the rule as it is.

## 3. Condition kinds

Each row costs the poll one read per actor and situation; none allocates or logs. A value
whose plugin is not loaded never holds. "Verified" means the rule runs in Livery today.

| Kind | Values | Holds while | Source | Verified |
|---|---|---|---|---|
| In Combat | none | the actor is in combat | `Actor::IsInCombat` | yes (as today's rule) |
| Sleeping | none | the Sleep preset's rule | `GetSitSleepState`, CanSleep furniture | not yet |
| In Water | none | the in-water rule: swimming, or water no lower than 40 units under their feet; once in, out only past 60 units; on a mount swimming only | `IsSwimming`, `GetWaterHeight` | wading yes |
| Swimming | none | the actor state's swimming flag | `ActorState::IsSwimming` | not yet |
| Indoors | none | their cell is an interior cell | `TESObjectCELL::IsInteriorCell` | no (`IsExteriorCell` is used by Cold) |
| Location type | Town, Castle, Player home, Dungeon | their current location's own keywords: Town = the Town preset's keywords (`LocTypeCity`, `LocTypeTown`, `LocTypeSettlement`, `LocTypeDwelling`, `LocTypeInn`, `LocTypeHouse`, `LocTypePlayerHouse`; `LocTypeHouse` for the player too, unlike the preset), Castle = `LocTypeCastle`, Player home = `LocTypePlayerHouse`, Dungeon = `LocTypeDungeon` | `BGSLocation::HasKeywordString` | Town yes; Castle, the rest not yet |
| Place | a list of locations and cells | the memo's place rule: their current location or any of its parents (`parentLoc`), or their cell, is in the list. The open country belongs to no place: most of its cells have no location (Skyrim.esm: 930 of Tamriel's 11,187 exterior cells have one, one of them a hold's), so `GetCurrentLocation` is null there | `GetCurrentLocation`, `GetParentCell` | yes (a home, a city; the open country checked null) |
| Weather | Rain, Snow, Snowy region | the Cold preset's weather rule without its cell test: Rain = the deciding weather has `kRainy`, Snow = `kSnow` (while the sky blends, the last weather decides until the new one is halfway in); Snowy region = the sky's region has snow weathers for a quarter of its weather chance | `Sky::currentWeather`, `lastWeather`, `currentWeatherPct`, `region` | Rain and Snow yes; Snowy region not yet |
| Time of day | From, To (game hours, minutes allowed) | the game clock is in the range; a range whose To is before its From wraps midnight; default 20:00 to 6:00 | `Calendar::GetHour` | no |
| Spell | a list of `MagicItem`s | any active effect on the actor comes from one of them (memo: Magic conditions) | `MagicTarget::GetActiveEffectList`, `ActiveEffect::spell` | no |

Notes:

- In Water keeps its hysteresis in a per-actor list of those last told in water
  (`g_inWater`, changed only as someone goes in or out). Swimming needs none: the flag is the
  engine's.
- Indoors is the cell's kind. Worldspaces that are underground or elsewhere (Blackreach,
  Sovngarde, the Soul Cairn) count as outdoors; a known quirk, documented, not worked around.
- Location type reads the location's own keywords only, as the presets do (a room of a city is
  not in it by its parent); Place walks the parents. A hold is not offered as a Place (decided
  2026-11-13): it would hold in the hold's towns, farms, camps and dungeons only, never on the
  roads between them, which is not what its name promises; a region condition (§10) would
  cover the open country.
- Weather does not test the cell: the user adds `not Indoors` for outdoors only. The Cold preset
  reads as `Weather {Rain, Snow, Snowy region}` and `not Indoors`.
- Time of day: game time, so waiting and sleeping jump the clock; the poll sees the new hour at
  its next run. From equal to To is the whole day.
- Spell: the condition names spells, powers, shouts, potions and abilities (`MagicItem`), not
  magic effects, for the reasons in the memo; the window may still say "effect" where the
  player would. Whether `kInactive` and `kDispelled` effects must be skipped is unverified
  (§10).

## 4. Pending kinds

Not offered in the first version; added on request. The rule each would use:

- Drawing Weapon: `ActorState::IsWeaponDrawn`.
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

`Situations::Evaluate(actor, rows)` keeps its shape: for each situation in `Priority()`, add it
when it applies. It reads only the situations whose row in the actor's rows holds an outfit
(`Assignments::SituationsNow` passes them): one they have no outfit for cannot be worn, so
`OutfitRows::Resolve` gives the same row, and with none it returns before any engine read.
For a preset that is its fixed rule, and never in combat. For a user-defined situation: in
combat, only with `Apply in combat` on; then every row must hold (`SituationStore::RulesOf`).
What a row reads of the actor (the location, in water) is read once per evaluation. A row with a
list of forms compares FormIDs resolved as the store reads or saves the rules
(`PieceFile::FormIDOf`) and kept with them (`SituationRules::Place::formID`), so the poll
compares integers and walks parents (at most 32, against a loop in a mod's data); no name lookups, no allocation,
no logging. The poll, its one-redress-per-run rule and the 3 s hold are unchanged.

The registry is mutable: `SituationStore::Load` adds the file's user situations at
`kDataLoaded`, before any co-save load, and `SituationStore::Create` adds one after its write
took, both on the game thread; the window is open then, so the poll does not run. Each list is
reserved for `kMaxSituations` (64, the Default and the presets included), so adding moves
nothing. A removed situation stays in the registry and the store's table until the game is
closed, so no id moves or is reused; it counts toward the 64 until then, and Create refuses
past them. `OutfitRows::Rows` reads empty past what it stores, so a new situation needs no
row change.

## 6. Data

situations.json becomes version 2 when the first user-defined situation is made (`nextKey`
above 1), and stays so after every one is removed; until then it is written as version 1, so
a Livery without user situations still reads it. The parser reads version 1 (presets' names
and removed flags only) as version 2 without user situations. The presets' entries are as in
version 1; the user's follow them in the order they were made. Shape:

```json
{
  "version": 2,
  "nextKey": 3,
  "situations": [
    { "key": "Town", "name": "City", "removed": false },
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
  retired key today: dropped.
- Form values are stored as a piece is (`PieceFile::PieceRef`: plugin file name, local FormID,
  the name for the reader); a value whose plugin is not loaded stays in the file and never
  holds.
- The parser refuses unknown kinds, values and keys as it refuses unknown versions today; the
  file is then read-only for the session (`SituationStore::Load` sets the writable state once).
- The same write rules as every shared file (CLAUDE.md §2.1): the write comes first, through a
  temporary file; a refused or failed write changes no row.

## 7. Situations page (provisional)

The prototype decides the final form; 2a built this (`src/SituationPage.cpp`, Condition editor).

- The page's button row gains `New Situation...` (before Exit): the `New situation` popup asks
  for a name (Create / Cancel, as the New chest popup), saving the page's edits first; the new
  situation, with no conditions, is then selected with its details shown. A user-defined
  situation has Remove (the same popup and row clearing as a preset's Remove, without the
  Restore Presets line) and the condition editor below the name.
- The editor: one line per row with a `Not` checkbox, the kind (its tooltip says what it
  means) and Remove at the line's right end; under it, indented, the values of a kind with a
  small fixed set (Location type, Weather) as checkboxes, side by side while the pane has
  room, or for Time of day `From [time] to [time]`, two dropdowns in 30-minute steps (a time
  read from the file off those steps shows as it is). Then `Add Condition...`, a dropdown of
  the kinds, closed by the choice, which adds a row with nothing ticked (a Place row opens the
  Add place popup at once). Place lists its places, one a line with
  Remove at its right end (one whose plugin is not loaded greyed, saying so on hover), then
  `Add Place...`; Spell will do the same.
- `Add Place...` opens the `Add place` popup, its choices read as the window opens
  (`Situations::PlacesAround`): the actor's current location; `This room only (<cell>)` for an
  interior cell, `This area only (<cell>)` for an exterior one (a cell without a name shows its
  editor ID, else its FormID). It opens on the location, or on the cell when they are outside
  any. Add is greyed for a place the row has already.
  Spell's add opens the memo's picker (known spells first, all spells by search).
- `Apply in combat` is a checkbox under `Add Condition...`; with an In Combat row (not
  negated) it is on and disabled.
- The `Applies` line, under its own label, replaces `ConditionsText` for a user-defined
  situation and is built from the rows in order, one phrase per row, `not` first: `In a town
  or a castle, in rain or snow, not indoors, at Whiterun, from 8 PM to 6 AM, while Stoneflesh
  is on.` A row with nothing selected reads `Weather (nothing selected)`; a situation without
  rows `Never applies.`
- Edits save as the page's other edits do: 0.5 s after the last one or as the page is left,
  the file first. Nobody is redressed for changed conditions then: the poll, which waits
  while the window is open, follows them once it closes.

## 8. Priority

Open. Where user-defined situations stand among Sleep, Cold, Home, Castle and Town, and whether
they and the presets are ordered as one list, is decided after the prototype. Constraint from
the memo: a Place situation for a player home must come before Home. Provisional, as built:
every user-defined situation before the presets, in the order they were made
(`Situations::AddUser`).

## 9. Implementation order

1. 2a: the condition model, situations.json version 2, the mutable registry, the editor, and
   the kinds that need no form picker: In Combat, Sleeping, In Water, Swimming, Indoors,
   Location type, Weather, Time of day. Usable on its own; the prototype of §7 is checked here.
   Built on `feat/cs-model`, checked in game.
2. 2b: Place, with its list line and the add-current-place popup (the first form picker).
   Built on `feat/cs-place`; in-game check pending.
3. 2c: Spell, reusing 2b's list line, with the search picker and the active-effect walk.

## 10. Open and in-game checks

- Whether `MagicTarget::HasMagicEffect` and the active effect list skip `kInactive` or
  `kDispelled` effects, or the walk must test the flags.
- Which Skyrim.esm locations carry `LocTypeDungeon`, and whether the DLC dungeons do.
- What `Sky::currentWeather` holds in an interior cell (the last exterior weather is assumed).
- A Time of day range that wraps midnight, at the wrap.
- Whether an NPC's parent cell follows them across exterior cells (a `This area only` place).
- The open country belongs to no hold in the data (§3): a region condition (the cell's
  regions, `TESObjectCELL::GetRegionList`; the Cold preset reads the sky's weather region,
  `Sky::region`) would cover it, if wanted; regions have editor IDs only (`WeatherTundra`).
- OR groups, and the priority of user-defined situations, after the prototype (§2, §8).
