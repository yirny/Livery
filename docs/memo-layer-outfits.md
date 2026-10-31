# Memo: layer outfits

Decided 2026-10-24, deferred until after the first release. A layer outfit is worn over the
outfit beneath instead of in its place: a cloak and a hat for Cold over whatever the actor wears
otherwise, without one full outfit per base and layer pair. Not implemented; nothing below is
current behaviour.

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
- Resolution (`OutfitRows::Resolve`): walk the applicable situations, most specific first, and
  take the first row whose outfit is a layer and the first whose outfit is not; the Default ends
  the walk. A Default holding a layer with no base found means the base is the actor's own
  clothes: the layer is dressed with `Dress` told not to take off what is outside the outfit, and
  the own pieces it pushes off go to `takenOff` as today, back on at release.
- The record keeps the base beside the layer: `wornOutfit` and `worn` plus the base's outfit and
  row (a new co-save version; 5 is in use, design.md §5); the poll's change check compares
  all four.

## Open points

- A random row whose list mixes layers and non-layers: the pick decides the layer type, or the
  window refuses the mix (the Situations page's drops). Decide before the UI.
- Every place that names the worn outfit shows two: piece headings, HUD messages, the
  Wardrobe page's Apply popup, Remove's who-wears-it list, `Assignments::Choices`.
- The preview of a layer outfit should show it over the current base.
- Unit tests for the merged resolution and the slot merge.

## In-game checks the design rests on

- The engine pushes off a worn piece by biped-slot overlap, so the slot-mask merge matches what
  equipping the layer piece would do.
- A layer over the actor's own clothes: the pushed-off own piece (unlocked, in the inventory)
  is not put back by the engine's best-gear pass through a slot the layer piece leaves free.
  The same kind of gap as the empty slots docs/design.md §2 leaves to the engine.

## Files it touches

`src/Library.cpp` / `.h` (the flag, JSON), `src/OutfitRows.h` (resolution), `src/Assignments.cpp`
(record, the new co-save version, `ArmorFor` merge, change check), `src/Dressing.cpp` / `.h` (the
no-strip flag), the window pages that name the worn outfit, `test/`, docs/design.md §2, §5, §7.
