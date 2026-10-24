# Capture window redesign

Written 2026-10-19 for the AI that implements it. Decided with the user from their draft of
this file; current spec only. It reworks the existing window (design.md §8 item 7) in three
steps, each built, checked in game and committed before the next: (1) the Capture page,
(2) the Save page, (3) Apply. Every step needs CLAUDE.md §1 approval first. Docs follow the
code: design.md §7 and §8, README, and CLAUDE.md §2.1 change in the step that makes them stale.

## 1. Capture page (today's Pieces page)

- Title: `#{Name}'s Inventory`, `#{Name}` the actor's name. The summary line under it stays.
- The piece list is grouped under headings, in this fixed order, a heading with no piece hidden:
  1. `Outfit ("#{Outfit}")`: worn pieces Livery put on from a library outfit,
     named by the outfit worn now (the default's or the worn situation's).
  2. `Outfit (Not in Wardrobe)`: worn pieces Livery put on that are not a saved
     outfit (outfit id 0, or the outfit has left the library).
  3. `Outfit (Unmanaged)`: worn pieces Livery did not put on.
  4. `Inventory`: carried, not worn. The actor's own outfit copies
     (`ExtraOutfitItem` naming their base's `defaultOutfit`) are left out while unworn, as the
     trade menu leaves them out (design.md §3).
  5. `#{Player}'s Inventory`, the player's name (`Player` when it has none; NPC target only): the player's armor, per the settings (§2);
     a form the NPC also carries is listed once, under 1–4.
  6. `"#{Outfit}"`: pieces of the loaded outfit nobody carries, listed while
     it is loaded (today's `owned == false` rows).
- Three columns: Name, Type, Slot; Slot only with `ShowSlotColumn` on (INI `[Window]`, SKSE
  Menu Framework; off by default), else Type ends at the list's right end. Type's strings and rules are SkyUI's, from the biped
  slots, taken from the SkyUI source (schlangster/skyui on GitHub, the inventory data setter)
  at implementation time, never from memory. Slot lists the piece's body slots by number,
  lowest first (`32, 34`), sorts by the lowest, and is as wide as its widest text up to four
  slots; a longer list ends in an ellipsis. A click on a
  column header sorts by that column, a second click reverses the order; the headings always
  come first; Name ascending to start.
- The count line `x of y piece(s) selected` moves under the list, ending in `Add pieces from
  Wardrobe...` (the Load page; docs/outfit-management-ui.md §2). Live Preview stays as it is.
- Assign for and `Situations...` go, with the Situations page (§4 covers assignment).
- Buttons: `Save` (needs a ticked piece; opens the Save page), `Apply` (needs a ticked piece),
  `Exit`. `Restore` at the right end of the button row: a borderless text
  button lit on hover, with a `(?)`, shown only while Livery dresses the actor. It ends
  Livery's dressing of them. Its `(?)`: "Takes Livery's outfit off #{Name} and forgets their
  situational outfits. Items moved from your inventory stay with them."
- Library: the group "Armor Type" becomes "Class" (constant, Sort key, `No Class` filter row)
  with SkyUI's four class names, Jewelry new. `DetectArmorClass` follows SkyUI's rule. Where the
  stored names change, `Load` maps the old ones (Light Armor, Heavy Armor, Clothing); the file
  is rewritten at the next Create or Update, never at load.
- Theme: a header row widget and a multi-column `TickRow`, a borderless text button.
- Needs: `Assignments::Choices` exposes the armor FormIDs of the pieces Livery has on them.

Checks: headings and columns on an NPC Livery dresses in a library outfit, on one it dresses in
loose pieces, on an undressed NPC and on the player; Restore on a dressed NPC; sorting.

## 2. Settings

INI + SKSE Menu Framework, nothing in the window. Names provisional.

| Key | Default | Meaning |
| --- | --- | --- |
| `[Window] ShowPlayerInventory` | 1 | NPC target: list the player's armor under heading 4 |
| `[Window] ShowPlayerWornPieces` | 0 | ...including what the player wears |
| `[Transfer] MoveFromPlayer` | 1 | Apply moves the player's instance to the NPC (§3); off: a plain copy, as today |
| `[Transfer] ProtectFavorites` | 1 | favorites are left out of heading 4, as quest items always are |

## 3. Transfer

- At Apply only, for ticked pieces under heading 4: `TESObjectREFR::RemoveItem(armor, 1,
  ITEM_REMOVE_REASON::kStoreInContainer, extraList, npc)` moves the instance with its extra
  data (the container menu's path). The instance is chosen as Dress does: enchanted, else the
  most tempered, else plain. Then Dress finds it in the NPC's inventory.
- Quest items (`InventoryEntryData::IsQuestObject`, per form) and, with ProtectFavorites,
  favorites (`IsFavorited`) are not listed. Heading 4's `(?)` says so.
- The NPC's inventory is read again after the moves. A piece that did not move is dropped
  from Dress's list and counted in the HUD message; never a silent copy.
- A moved instance is not `added`: Restore leaves it with the NPC, as a traded item.
- The preview and Create / Update Outfit move nothing.

Checks: an enchanted piece moves with its enchantment; a quest item and a favorite are not
listed; the HUD counts moved pieces; the NPC keeps the piece after Restore.

## 4. Save page

Reached from `Create / Update Outfit` with the ticked pieces, which both tabs save. The page
writes the library; only Create and Apply gives the outfit a row. Giving an outfit to a
situation is Manage Situations' job (docs/situation-management-ui.md).

- Heading: `Create New Outfit / Update Existing Outfit` in place of the title, in the
  heading font, where the title goes (Nordic: centred, over its rule), separated by a greyed
  `/`, text only (no box), the unselected one half transparent (`Theme::BeginHeadingTabs`);
  no actor name or summary line. The page is widened when the tabs would not fit on one
  line. Each tab keeps its name while the window is open, the Update tab its outfit.
- Both tabs start with the name, as Manage Wardrobe's field: no label (`Outfit Name` on hover
  while not being typed in; a failed save's error instead).
- `Create New Outfit`: the name, starting from the first free `Captured Outfit NNN`, then the
  ticked pieces read only under their count line, as Manage Wardrobe shows an outfit's.
  Buttons `Create`, `Create and Apply`, `Back`; Enter is Create.
- Create and Apply saves as Create does and gives the new outfit the row Manage Wardrobe's
  Apply would (docs/outfit-management-ui.md §4), through the same popup body
  (`ApplySubject`), its question starting `Create "#{Outfit}", then`:
  - while the actor wears another outfit for a situation: `Lydia is wearing their Town
    outfit, "B".`, `Create "A", then make it their new Town outfit?` (random on: `add it to
    them and wear it now`); Yes gives it that row, Apply as Default (?) the Default;
  - else the Default; when the Default holds an outfit or pieces it asks first: `Create "A",
    then apply it as Lydia's Default?`, `The current Default, "B", will be replaced.`,
    `Create and Apply` / `Cancel`; else it saves at once.
  Cancel saves nothing. The Player's Inventory pieces move first, and those that do not move
  stay out of the one dressing (`SaveRequest::fromPlayer`, `Assignments::Changes::leaveOut`).
- `Update Existing Outfit`: greyed while the library is empty (`kNoOutfits`). The page widens
  for three equal columns (`kWideWidthRatio`, 1.05 of the screen height) under the name: the
  outfit list (docs/outfit-management-ui.md §1, with its own search and sort), `CURRENT
  PIECES` (the chosen outfit's pieces as the library has them, its count line counting those
  of unloaded plugins; `Select an outfit to update` while none is chosen) and `NEW PIECES`
  (the ticked pieces). Each column's list grows with its rows up to `kMaxListHeightRatio`,
  then scrolls. The tab opens with its outfit, or else the one last loaded, or else the
  library outfit Livery has on the actor, chosen. A click on an outfit chooses it, fills the
  name from it and shows it on the preview; a second click unchooses it, which greys the name
  and shows the ticks. Choosing the outfit already chosen keeps the name typed. Create and
  Back show the ticks again. Buttons `Update #{Outfit}` (`Update`,
  greyed with `kNeedOutfit`, while none is chosen) and `Back`; Escape clears the search
  first.
- The name may be changed: a name must be unused, ASCII case ignored, by any other outfit
  (CLAUDE.md §2.1); `Library::Update` takes the name.
- Update's confirm popup (`Replace pieces?`): `Replace the N piece(s) of "A"` / `with the M
  ticked piece(s)?`, a line for the pieces of unloaded plugins that stay in it and one for a
  rename, then `Confirm` / `Cancel`. No row changes.
- Order: the library write; on failure `SetError` and nothing assigned; on success Close (the
  preview restores), then Create and Apply's row in one call that redresses once, or after
  Update the captured actor dressed again when they wear the outfit (`RedressFor`); one HUD
  message for the library and one for the dressing.

Checks: Create saves and changes no row; Create and Apply while wearing a Town outfit asks
(Yes, Apply as Default, Cancel saves nothing), with a Default held asks before replacing it,
and with neither dresses at once; Update renames, changes no row and dresses the actor again
when they wear it; a failed write assigns nothing.

## 5. Apply

- Apply assigns the ticks as the default, after the transfer (§3).
- Modal `Update "X" with the ticked pieces?` when Livery dresses the actor, the outfit worn
  now is a library outfit (the default's id when the default is worn, else the worn
  situation's) and the ticks differ from its loaded pieces. For a situation it adds `"X" is
  #{Name}'s Town outfit.`
  - `Yes`: `Library::Update(X, pieces = ticks, name kept)`. Default
    worn: then `Assign(ticks, X)`. Situation worn: the default is not touched; a forced
    redress puts the updated outfit on (new Assignments API; `WearFor` only redresses on a
    change).
  - `No, apply as is`: `Assign(ticks, 0)`: the ticks become the Default's pieces, `chosen`
    0, random off. Its `(?)`: the pieces become #{Name}'s Default as they are, without a
    saved outfit, so editing the library does not change them; when a situation row holds an
    outfit, it adds that while a situational outfit is worn the new Default shows only when
    that situation ends.
  - `Cancel`: back to the window.
- Apply is greyed with no ticked piece (`kNeedPiece`).

Checks: the modal on a dressed NPC with one piece unticked; no modal when the ticks equal the
outfit; Yes while a Town outfit is worn updates that outfit and the NPC wears it at once; No
in a town changes nothing visible until the NPC leaves.

## 6. Removed

- Assign for, `Situations...` and the Situations page (§1, §4).
- The Tailor export: `src/TailorExport.cpp/.h`, its CMake line, the three `TailorExport::Write`
  calls in `src/Library.cpp`, design.md §4 and §6 entries, §8 item 6, the README section.

## 7. Situational outfits: model and roadmap

Situation data (name, later conditions) is global, in `situations.json`. Per
actor, per row (the Default and each situation): a list of library outfits, a random flag
and the outfit worn with random off. Spec and steps: docs/situation-management-ui.md.

## 8. API changes

- `Assignments::Choices`: the armor FormIDs Livery has on the actor (§1).
- `Assignments`: clear the default alone; apply several assignment changes with one redress;
  force a redress of the worn situation outfit (§4, §5).
- `Library::Update`: the name (§4).
- `Settings`: the four keys of §2 and their menu entries.
- `Theme`: column header row, multi-column `TickRow`, text-only tabs, borderless text button.
