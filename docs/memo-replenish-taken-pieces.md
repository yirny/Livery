# Memo: replenishing pieces taken back in the trade menu

Under consideration since 2026-10-24; not current behaviour. Today a piece the player gave
an NPC and takes back stays taken (`docs/design.md` §2, Keeping the outfit on): the engine's
next re-evaluation fills the slot from the NPC's own armor, the window's summary line says
`Assigned "X" as Default, 1 piece not worn.`, and Manage Wardrobe's Apply puts a copy on.

The idea: when the player takes back a piece of an NPC's outfit, Livery adds a plain copy at
once, as Apply's step 2 does, so the outfit never changes by itself.

- Not a dupe: the copy is one of Livery's (co-save `added` key), so the trade-menu filter
  (§3) hides it; the player ends with what they would have had by applying without giving.
- `TESContainerChangedEvent` sink, shaped like the `TESObjectLoadedEvent` one in
  `src/Assignments.cpp`: only `newContainer` = the player and `oldContainer` in the managed
  list queue an SKSE task; everything else returns after two integer compares. It fires on
  every pickup and trade, so it is a per-event path (CLAUDE.md §3).
- The task puts one piece on with `PutOn(lock = true, addMissing = true)`; no full redress,
  which would remove and remake the copies (new keys) and drop the 3D once.
- Guard 1: replenish only when no copy of that armor is still worn, so giving and taking the
  same piece again does nothing while the copy is on.
- Guard 2: never replenish a taken copy of Livery's own. Where the filter does not hide the
  copies (an NPC without `defaultOutfit`, a runtime without call sites) taking it again and
  again would be a dupe.
- Only takes to the player: other mods' `RemoveItem` loops (stripping mods) would otherwise
  ping-pong with Livery.
- In-game checks: the trade menu's list refreshes, and passes the filter, when the copy is
  added with the menu open; the model rebuild in the trade menu.
- Docs it would change: `docs/design.md` §2 step 2 (copies made only at assignment time),
  Keeping the outfit on, and Items (where copies are made).
