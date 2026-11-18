# Memo: user-defined situations, place and magic conditions

Decided 2026-11-11 and 2026-11-13, not implemented; nothing below is current behaviour.
Livery's situations are a fixed set (`Situations::Situation`, `kPriority`, `ConditionOf`;
docs/design.md §7). The plan item "user-defined conditions in the way of Skyrim Outfit
Equipment System NG" (§8) extends them to situations the user defines: a key, a name, a list
of conditions, a place in the priority order. Two condition kinds are decided: a place the
actor is in (a multi-cell player home, whose interior, basement and exterior are one situation
apart from the other player homes) and a magic effect on the actor ("a different outfit while
Stoneflesh is on"). The place condition ships first.

## Decisions

### Place conditions

- A place condition is a list of places; it holds while the actor's current location
  (`TESObjectREFR::GetCurrentLocation`), or any of its parents (`BGSLocation::parentLoc`),
  is one of them, or while their cell is one of them. A place is a `BGSLocation` or a
  `TESObjectCELL`, stored as plugin name + local FormID like a piece (`PieceFile::PieceRef`),
  so the list survives load order changes; a place whose plugin is not loaded stays in it as
  not holding. The user-defined situation is the group: no separate "custom location" record.
- Registration: the Situations page adds the place the captured actor stands in to a new or an
  existing user-defined situation. The popup names what would be registered. By default
  that is the cell's location; `This Cell` registers the cell
  instead, for a cell without a location of its own. A hold is not offered (2026-11-13): the
  open country has no location in the data, so a hold would hold in its named places only.
  The window freezes the game, so the actor's place is where they stand.
- Per poll it costs a FormID comparison against a short list and a walk up the parents; no
  allocation, no logging.

### Implementation order

1. Done: the registry (`Situations::Count`, `Choosable`, `Priority`, `kMaxSituations`) holds
   the presets; `Situation` ids past `kCold` are free for user-defined situations;
   `OutfitRows::Rows` and `SituationTable::Table` are sized by the registry. situations.json
   stays version 1 and the co-save format is unchanged.
2. The place condition and the Situations page's part: a new situation, the list of its
   places with Remove, the add-current-place popup. situations.json becomes version 2 when
   it first holds a user-defined situation; version 1 then needs a migration read, since the
   parser refuses any other version. The registry is built once today: adding a situation
   at runtime needs it mutable (game thread only), and every assignment's `Rows` and the
   store's table grow with it.

### Magic conditions

- Magic conditions name a spell, not a magic effect: a `MagicItem` (spell, power, shout,
  potion, ability), stored as plugin name + local FormID. The condition holds while any active
  effect on the actor comes from one of the named spells (`ActiveEffect::spell`,
  `RE::MagicTarget::GetActiveEffectList`, flags `kInactive` / `kDispelled` to be honoured once
  checked in game). One condition lists several spells, OR-ed.
- Why spells: the effects Stoneflesh and its family apply are named `Armor - Stone`,
  `Armor - Oak`, `Armor - Iron`, `Armor - Ebony`, `Armor - Dragonhide`, so a search for
  "Stoneflesh" over effects finds nothing; and 136 visible effect names in Skyrim.esm are shared
  by several effects (`Restore Health` ×3, `Fortify Health` ×3, ...). Spells carry the names
  the player knows, and potions, shouts, racial powers and shrine blessings are `MagicItem`s
  too, so one picker covers them all.
- Picker: a search field with two scopes. `Show known effects` (default): the actor's known
  spells (`Actor::addedSpells`, the NPC record's and the race's spell lists) plus the spells
  behind the effects on them now. `Show all effects`: every spell, power, shout and potion of
  every loaded plugin, listed only once a search term is typed (thousands with mods). The
  scope labels are provisional; propose player-facing wording with the UI. Same names are
  told apart by a tooltip with the plugin name and FormID.
- A spell whose plugin is not loaded stays in the condition as not holding, as the library
  keeps pieces of unloaded plugins.
- Keywords are not offered in the first version. Skyrim.esm (950 MGEF, 778 named and not
  HideInUI, 432 of those with keywords) has family keywords where they matter:
  `MagicArmorSpell` (all five flesh spells, plus a hidden Telekinesis ability effect),
  `MagicCloak`, `MagicInvisibility`, `MagicWard`, `MagicBlessing` (11 shrine blessings),
  `MagicSummon*`, `MagicNightEye`; but keywords have no player-facing names, 45% of visible
  effects carry none, and mod spells vary. A later version may offer "any spell of the same
  kind" derived from the keywords of the spell the user picked.
- Per situation, a checkbox `Apply in combat` (wording provisional). Off keeps today's rule
  (`Situations::Evaluate` returns nothing in combat, the Default is worn); its tooltip says
  that in combat the Default outfit is worn while it is off. Magic conditions are mostly
  meant for combat, so a magic situation normally has it on.
- Known limit: the poll runs once a second and holds a redress for 3 seconds, so an effect
  that lasts a few seconds is not followed; a 60-second flesh spell is.

## Open

- The shape of a user-defined situation in `situations.json` (version 2: the parser now
  refuses unknown keys): key, name, removed, conditions, priority, apply-in-combat. The
  co-save stores situations by key string already, so new keys need no co-save version.
- Which other condition kinds ship with the first version (location keyword, weather, actor
  keyword, faction, worn item), and how conditions in one situation combine (AND, as the
  presets' rules do).
- Where user-defined situations stand among Sleep, Cold, Home, Castle and Town: a place
  situation must come before Home, which applies in a player home too; the rest is decided
  once the other condition kinds are.
- How the Situations page edits the list: adding, removing, reordering a situation; whether
  the presets become editable in the same way or stay fixed.
- Verify in game: whether `MagicTarget::HasMagicEffect` skips inactive or dispelled effects, or
  the list must be walked by hand.
- Colouring each condition in the Applies text (requested 2026-11-18): green (`#B7DA7B`) while it
  holds now, red while it makes a situation higher in the list never worn, grey otherwise;
  tooltips `Holds now.` / `Makes {situation} never worn.` / `Does not hold now.`
  - Green: the window has no live values. `RowHolds` (Situations.cpp) is game-thread only, so the
    page needs a new path that hands the unsaved rules to the game thread and gets a result
    per row back, refreshed while the popup is open (weather and time change under it).
  - Red: `CoveredBy` marks a pair only when every row of the lower situation is implied by
    a row of the upper one, so on the lower situation every row would be red at once. A
    per-row colour adds nothing; one line over the Applies text (the reverse of the
    never-worn text) fits what is known.
  - Which colour wins when a row is both.
  - Palette: red is the existing `Error` key; add only `Positive` (sarrai-common, a key left out
    keeps the skin's colour, so no file needs rewriting).

## Engine facts (Skyrim.esm, read 2026-11-11)

- Flesh spells and their visible effect: Oakflesh `ArmorFFSelf0` 00051B15, Stoneflesh
  `ArmorFFSelf25` 00059B7A, Ironflesh `ArmorFFSelf50` 00059B7B, Ebonyflesh `ArmorFFSelf75`
  00059B7C, Dragonhide `ArmorFFSelf100` 000CDB75; each spell also applies three hidden perk
  effects (`PerkArmorFFSelf30/50/70`).
- MGEF full names are localised strings (`strings/skyrim_english.strings` inside
  `Skyrim - Interface.bsa`); keywords (KYWD) have no full name.
- `ActiveEffect` keeps `spell` (`MagicItem*`, offset 0x40) and `effect` (`Effect*`, 0x48);
  `Actor::addedSpells` is the learned spell list.
