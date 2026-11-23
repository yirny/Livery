# Memo: layer outfits

Decided 2026-10-24, deferred until after the first release. A layer outfit is worn over the
outfit beneath instead of in its place: a cloak and a hat for Cold over whatever the actor wears
otherwise, without one full outfit per base and layer pair. With NPC groups (docs/npc-groups.md)
it covers what Wet and Cold's gear does: a hood and a cloak over every NPC's clothes in the rain.
Not implemented; nothing below is current behaviour.

## Decisions

- Name: layer. The library flag `layer` (off by default); the window ticks it as
  `Layer over the outfit beneath`; what is worn shows as `B over A`.
- A layer is a result of resolution, never a state: every dressing derives the base and the
  layer from the rows anew. So a base change keeps the layer while its situation still applies
  (walking into town in the rain keeps the cloak), a layer comes off when its situation ends and
  the base is dressed again whole, and a layer never goes over a layer. None of these are stored
  rules.
- Dress one merged list through the existing single-layer path: the base's pieces whose biped
  slots do not overlap a layer piece's, then the layer's pieces. A base piece the layer would
  push off is never put on, so `PutBackOn` at 3D load cannot put it back and push the layer off.
- A layer outfit has a second library flag, off by default (decided 2026-11-21; name and label
  to settle before the UI): the outfit beneath keeps its items. On, the merged list is the
  layer's pieces whose biped slots do not overlap a base piece's, then the base's pieces whole,
  so a guard keeps the helmet and gets only the cloak. Off, the layer wins, as above.
- Resolution (`OutfitRows::Resolve`): walk the applicable situations, most specific first, and
  take the first row whose outfit is a layer and the first whose outfit is not; the Default ends
  the walk. A Default holding a layer with no base found means the base is the actor's own
  clothes: the layer is dressed with `Dress` told not to take off what is outside the outfit, and
  the own pieces it pushes off go to `takenOff` as today, back on at release.
- The record keeps the base beside the layer: `wornOutfit` and `worn` plus the base's outfit and
  row (a new `ASGN` version; 6 is in use, design.md §5); the poll's change check compares
  all four.
- Selection chance (decided 2026-11-22; docs/situation-management-ui.md §1 `chances`): a random
  row's list may mix layers and non-layers; the non-layers are drawn first, as today, then the
  layers apart from them. Among the layers the chances are ratios as for the base, but a list
  whose layers are all below 100 may wear no layer: that happens with probability
  Π(1 − min(c, 100) / 100) over its layers, so one layer at 70 is worn 70% of the time and one
  at 100 or above means a layer is always worn. A draw of no layer is kept as a pick is: the
  record holds the layer's row with no outfit until that situation ends, so a redress (rain,
  a cell change, a row edit) does not draw again. The die never draws no layer. Its tooltip
  explains the threshold for a layer only.

## Open points

- Every place that names the worn outfit shows two: piece headings, HUD messages, the
  Wardrobe page's Apply popup, Remove's who-wears-it list, `Assignments::Choices`.
- The preview of a layer outfit should show it over the current base.
- Unit tests for the merged resolution and the slot merge.

## In-game checks the design rests on

- The engine pushes off a worn piece by biped-slot overlap, so the slot-mask merge matches what
  equipping the layer piece would do.
- A layer over the actor's own clothes: the pushed-off own piece (unlocked, in the inventory)
  is not put back by the engine's best-gear pass through a slot the layer piece leaves free.
  `Dressing::TakeOffExtras` (docs/design.md §2) empties a slot no piece has, so a layer over
  their own clothes must keep it from taking those clothes off.

## Files it touches

`src/Library.cpp` / `.h` (the flags, JSON), `src/OutfitRows.h` (resolution), `src/Assignments.cpp`
(record, the new co-save version, `ArmorFor` merge, change check), `src/Dressing.cpp` / `.h` (the
no-strip flag), the window pages that name the worn outfit, `test/`, docs/design.md §2, §5, §7.
