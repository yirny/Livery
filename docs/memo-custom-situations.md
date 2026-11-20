# Memo: user-defined situations, place and magic conditions

The decisions behind the place and spell conditions of docs/custom-situations.md, which is
the specification, the ideas not built, and the Skyrim.esm facts.

## Decisions

### Place conditions

- A place condition is a list of places; it holds while the actor's current location
  (`TESObjectREFR::GetCurrentLocation`), or any of its parents (`BGSLocation::parentLoc`),
  is one of them, or while their cell is one of them. A place is a `BGSLocation` or a
  `TESObjectCELL`, stored as plugin name + local FormID like a piece (`PieceFile::PieceRef`),
  so the list survives load order changes; a place whose plugin is not loaded stays in it as
  not holding. The user-defined situation is the group: no separate "custom location" record.
- A hold is not offered: the open country has no location in the data, so a hold would hold
  in its named places only.
- Per poll it costs a FormID comparison against a short list and a walk up the parents; no
  allocation, no logging.

### Magic conditions

- Magic conditions name a spell, not a magic effect: a `MagicItem` (spell, power, shout,
  potion, ability), stored as plugin name + local FormID. One condition lists several spells,
  OR-ed; how a spell is matched against the effects on the actor is in
  docs/custom-situations.md §3.
- Why spells: the effects Stoneflesh and its family apply are named `Armor - Stone`,
  `Armor - Oak`, `Armor - Iron`, `Armor - Ebony`, `Armor - Dragonhide`, so a search for
  "Stoneflesh" over effects finds nothing; and 136 visible effect names in Skyrim.esm are shared
  by several effects (`Restore Health` ×3, `Fortify Health` ×3, ...). Spells carry the names
  the player knows, and potions, shouts, racial powers and shrine blessings are `MagicItem`s
  too, so one picker covers them all.
- A spell whose plugin is not loaded stays in the condition as not holding, as the library
  keeps pieces of unloaded plugins.
- Keywords are not offered in the first version. Skyrim.esm (950 MGEF, 778 named and not
  HideInUI, 432 of those with keywords) has family keywords where they matter:
  `MagicArmorSpell` (all five flesh spells, plus a hidden Telekinesis ability effect),
  `MagicCloak`, `MagicInvisibility`, `MagicWard`, `MagicBlessing` (11 shrine blessings),
  `MagicSummon*`, `MagicNightEye`; but keywords have no player-facing names, 45% of visible
  effects carry none, and mod spells vary. A later version may offer "any spell of the same
  kind" derived from the keywords of the spell the user picked.
- Known limit: the poll runs once a second and holds a redress for 3 seconds, so an effect
  that lasts a few seconds is not followed; a 60-second flesh spell is.

## Engine facts (Skyrim.esm, read 2026-11-11)

- Flesh spells and their visible effect: Oakflesh `ArmorFFSelf0` 00051B15, Stoneflesh
  `ArmorFFSelf25` 00059B7A, Ironflesh `ArmorFFSelf50` 00059B7B, Ebonyflesh `ArmorFFSelf75`
  00059B7C, Dragonhide `ArmorFFSelf100` 000CDB75; each spell also applies three hidden perk
  effects (`PerkArmorFFSelf30/50/70`).
- MGEF full names are localised strings (`strings/skyrim_english.strings` inside
  `Skyrim - Interface.bsa`); keywords (KYWD) have no full name.
- `ActiveEffect` keeps `spell` (`MagicItem*`, offset 0x40) and `effect` (`Effect*`, 0x48);
  `Actor::addedSpells` is the learned spell list.
