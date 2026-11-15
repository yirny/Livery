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
  window opens, `Request::customSituations`) offers the editing: New Situation..., the order's
  drag and the condition editor. Off, every situation made stays in use, and the Situations page
  still lists them with their outfits, their Applies line read only, the never-worn mark, Remove
  and Restore Presets; renaming stays too. The poll does not read the setting.
- The first version ships the kinds of §3; §4 lists the kinds held back.

## 2. Condition model

A situation holds a list of condition rows. The situation applies while every row holds
(AND). A row is a kind, a `not` flag and, for a kind with values, a set of values; a row with
values holds while any of its values holds (OR), and `not` inverts the row. Rows do not nest:
there are no OR groups (*provisional*: revisited after use; so far every wanted rule
fits, "night or rain" being two situations). The same kind may appear in several rows
("Rain" and "not Snowy region"). A row with values and none selected never holds, so its
situation never applies; the Situations page says so (§7). A situation with no rows never
applies either.

Combat: every situation but the Default has the flag `Allow during combat`:
off, the situation does not apply in combat, so a fight is fought in the Default unless another
one applies; on, its rows decide. A situation with an `In Combat` row has the flag on and not
editable, since the row would never hold otherwise. Every preset has it off as installed.

## 3. Condition kinds

Each row costs the poll one read per actor and situation; none allocates or logs. A value
whose plugin is not loaded never holds. "Verified" means the rule runs in Livery today.

| Kind | Values | Holds while | Source | Verified |
|---|---|---|---|---|
| In Combat | none | the actor is in combat | `Actor::IsInCombat` | yes (as today's rule) |
| Sleeping | none | the sleeping rule of docs/design.md §7 | `GetSitSleepState`, CanSleep furniture | not yet |
| In Water | none | the in-water rule: swimming, or water no lower than 40 units under their feet; once in, out only past 60 units; on a mount swimming only | `IsSwimming`, `GetWaterHeight` | wading yes |
| Swimming | none | the actor state's swimming flag | `ActorState::IsSwimming` | not yet |
| Indoors | none | their cell is an interior cell | `TESObjectCELL::IsInteriorCell` | no (`IsExteriorCell` is used by Cold) |
| Location type | Town, Castle, Player home, Dungeon | their current location's own keywords: Town = `LocTypeCity`, `LocTypeTown`, `LocTypeSettlement`, `LocTypeDwelling`, `LocTypeInn`, `LocTypeHouse` or `LocTypePlayerHouse`, for every actor, Castle = `LocTypeCastle`, Player home = `LocTypePlayerHouse`, Dungeon = `LocTypeDungeon` | `BGSLocation::HasKeywordString` | Town yes; Castle, the rest not yet |
| Place | a list of locations and cells | the memo's place rule: their current location or any of its parents (`parentLoc`), or their cell, is in the list. The open country belongs to no place: most of its cells have no location (Skyrim.esm: 930 of Tamriel's 11,187 exterior cells have one, one of them a hold's), so `GetCurrentLocation` is null there | `GetCurrentLocation`, `GetParentCell` | yes (a home, a city; the open country checked null) |
| Weather | Rain, Snow, Snowy region | the Cold preset's weather rule without its cell test: Rain = the deciding weather has `kRainy`, Snow = `kSnow` (while the sky blends, the last weather decides until the new one is halfway in); Snowy region = the sky's region has snow weathers for a quarter of its weather chance | `Sky::currentWeather`, `lastWeather`, `currentWeatherPct`, `region` | Rain and Snow yes; Snowy region not yet |
| Time of day | From, To (game hours, minutes allowed) | the game clock is in the range; a range whose To is before its From wraps midnight; default 20:00 to 6:00 | `Calendar::GetHour` | no |
| Spell Effect (key `spell`) | a list of `MagicItem`s | a visible effect on the actor, neither inactive nor dispelled, comes from an item whose visible effects are exactly those of one of them | `MagicTarget::VisitEffects`, `ActiveEffect::spell`, `ActiveEffect::flags`, `MagicItem::effects` | no |

Notes:

- The presets as installed (`SituationTable::PresetRules`), each with `Allow during combat` off:
  Town = Location type {Town}; Castle = Location type {Castle}; Home = Location type
  {Player home}; Cold = Weather {Rain, Snow, Snowy region} and not Indoors; Sleep = Sleeping.
  Two changes from the first release's fixed rules, decided 2026-11-14 since a rule cannot
  depend on who the actor is: Home no longer holds in any house for an NPC (`LocTypeHouse`),
  and Town holds in any house for the player too. Town and Cold by rain, verified through the
  fixed rules, are checked again through the model (§10).
- In Water keeps its hysteresis in a per-actor list of those last told in water
  (`g_inWater`, changed only as someone goes in or out). Swimming needs none: the flag is the
  engine's.
- Indoors is the cell's kind. Worldspaces that are underground or elsewhere (Blackreach,
  Sovngarde, the Soul Cairn) count as outdoors; a known quirk, documented, not worked around.
- Location type reads the location's own keywords only, as the presets do (a room of a city is
  not in it by its parent); Place walks the parents. A hold is not offered as a Place, nor
  Tamriel above the holds (decided 2026-11-13): it would hold in the hold's towns, farms,
  camps and dungeons only, never on the roads between them, which is not what its name
  promises; a region condition (§10) would cover the open country.
- Weather does not test the cell: the user adds `not Indoors` for outdoors only, as the Cold
  preset does.
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
      "conditions": [{ "kind": "weather", "not": false, "values": ["rain", "snow", "snowyRegion"] }, { "kind": "indoors", "not": true }]
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
  retired key today: dropped.
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
  applies is worn.` as a list heading (`Theme::ListHeading`, its rule after it), then one
  heading per situation not removed in the order (`SituationForm::order`, request.situations'
  until a drag), each dragged by its grip (`kSituationPayload`, apart from the outfits') to
  another place among them, with `Request::customSituations`: over a heading's upper half
  before it, anywhere lower in its block after it, on the caption first; the drop shows as a
  line, as an outfit's does between outfits, and the situation's name follows the mouse as a
  tooltip. The grip (`Theme::SituationHeadingLook::grip`) is the heading's left end up to its
  name: while the heading is hovered, grip dots take the star's place; a click there without a
  drag selects. The label's help says so (docs/situation-management-ui.md §4).
- With `Request::customSituations`, the page's button row gains `New Situation...` (before
  Exit): the `New situation` popup asks
  for a name (Create / Cancel, as the New chest popup), saving the page's edits first; the new
  situation, with no conditions, is appended last, where it wins over every other, and
  selected with its details shown. Every situation but the Default has Remove (a preset's
  popup with the Restore Presets line, a user's without it) and the condition editor below
  the name.
- The editor, for every situation but the Default with `Request::customSituations` (off, the
  `Conditions` label over the Applies line, read only): each row reads as a sentence, with a ×
  (`Theme::ClearButton`, `Remove this condition.` on hover) at its line's right end, and the
  rows a field's gap apart (`Theme::SectionGap`). No `Not` checkbox: the row's `not` is a
  dropdown of two choices. A kind that is so or not (`SituationRules::TwoState`: In combat,
  Sleeping, In water, Swimming, Indoors) is one dropdown of its two states, `In combat` /
  `Not in combat`, `Indoors` / `Outdoors` (`KindInfo::label`, `negLabel`), the kind's tooltip
  on it. The others start with the kind's name (its tooltip says what it means), then the
  operator dropdown: `is` / `is not` with up to one value or form selected, `is any of` /
  `is none of` with more (as Linear and Airtable word it), `between` / `not between` for
  Time of day; then the values. Location type's and Weather's are a multi-choice dropdown
  (the Filter field's rows, staying open for several ticks) whose preview lists the ticked
  values, `Rain, Snow`, or a greyed `Select...` with none; Time of day's are `[time] and
  [time]`, two dropdowns in 30-minute steps (a time read from the file off those steps shows
  as it is). Both sit on the kind's line while they fit before the ×, else under it, 16px in.
  Place and Spell list their forms under the line, 16px in, one a line with a × at its right
  end (`Remove this place.` / `Remove this spell.`; one whose plugin is not loaded greyed,
  saying so on hover), then `Add Place...` or `Add Spell...`. Then `Add Condition...`, a
  dropdown of the kinds, closed by the choice, which adds a row with nothing selected (a Place
  or Spell row opens its Add popup at once); a two-state kind the situation has already is
  greyed there, a second row of it adding nothing. The label's help: `Applies while every
  condition holds.` and `A condition with several values holds for any of them.`
- `Add Place...` opens the `Add place` popup, its choices read as the window opens
  (`Situations::PlacesAround`): `This Location (<name>)`, the actor's current location; a
  `Parent Location (<name>)` for each location it is in, nearest first, up to the first hold
  or location holding a hold (Tamriel), left out with all above it, and of two in a row with
  the same name only the wider (`SituationRules::MergeSameNames`: Solitude's Avenues, a group of
  its houses without its streets, is named "Solitude" as the city is); and `This Cell (<name>)`,
  their cell (a cell without a name shows its editor ID, else its FormID); each followed by a
  (?) saying what it covers. The game's own terms, as "room" or "area" would blur the two. It
  opens on the location, or on the cell when they are outside any. Add is greyed for a place
  the row has already.
- `Add Spell...` opens the `Add spell` popup: `Add a spell to look for on <name>.`, a search
  field (`Search by name`, ASCII case ignored, as the outfit search), `Show all spells`, and a
  list ten rows tall of the spells whose name holds the search, a click picking one. Hovering a
  spell there or on the row shows its plugin and FormID, then its visible effects a line each,
  `- <name> (<editor ID>)`, the editor ID only when the game keeps one (it does not by itself;
  a mod such as powerofthree's Tweaks loads them). Off, the list is the actor's
  (`Situations::SpellsAround`, read as the window opens): the spells, powers and shouts they
  cast, learned (`addedSpells`) or of their NPC record and race (`actorEffects`, a shout by its
  words' spells), those with a visible effect delivered to themselves only (Flames or a Conjure
  never puts one on them; diseases, poisons and others' spells, which do, come through `Show all
  spells` or while they act), and the items behind the visible effects on them now (an ability, a disease
  or a potion only while it acts). On, every `SpellItem`, `AlchemyItem`, `ScrollItem` and
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
  is on.` A two-state row negated reads as its state, `not in combat`, `outdoors`. A row with
  nothing selected reads `Weather (nothing selected)`; a situation without rows `Never
  applies.`
- Edits save as the page's other edits do: 0.5 s after the last one or as the page is left,
  the file first. Nobody is redressed for changed conditions then: the poll, which waits
  while the window is open, follows them once it closes.
- While a situation is never worn (§8a), its details say so under the name, after a field's
  gap, in the error colour, wrapped: `Never worn: <name>, lower in the list, applies whenever
  this does.` Its heading carries a warning mark after the name (and its count), a circle with
  an exclamation mark in the error colour (`Theme::SituationHeadingLook::warning`), the same
  sentence its tooltip.

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

- the same kind and `not`, and A's values within B's (B's within A's with `not`): the value
  set, the time range, the list of places or spells;
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
   the kinds that need no form picker: In Combat, Sleeping, In Water, Swimming, Indoors,
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
- The open country belongs to no hold in the data (§3): a region condition (the cell's
  regions, `TESObjectCELL::GetRegionList`; the Cold preset reads the sky's weather region,
  `Sky::region`) would cover it, if wanted; regions have editor IDs only (`WeatherTundra`).
- OR groups (§2), after use.
- Town and Cold by rain through the model's rules, now that the presets' fixed code is gone.
