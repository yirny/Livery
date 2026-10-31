# Dress page, Save page and Apply

Written 2026-10-19 for the AI that implements it, decided with the user; current spec only.
The Dress page's piece list and buttons (§1), the settings and the transfer from the player's
inventory (§2, §3), the Save page (§4) and Apply (§5). The outfit list, the tab column, the
Wardrobe page and Remove are in docs/outfit-management-ui.md, the Situations page in
docs/situation-management-ui.md, the chests in docs/chest-ui.md; design.md §2 has the
dressing itself.

## 1. Dress page

- Title: `#{Name}`, the actor's name. The summary line under it stays.
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
     it is loaded (`owned == false` rows); a loaded chest's items too (docs/chest-ui.md).
- Three columns: Name, Type, Slot; Slot only with `ShowSlotColumn` on (INI `[Window]`, SKSE
  Menu Framework; off by default), else Type ends at the list's right end. Type's strings and rules are SkyUI's, from the biped
  slots, taken from the SkyUI source (schlangster/skyui on GitHub, the inventory data setter),
  or the Type an I4 rule gives (design.md §9). Slot lists the piece's body slots by number,
  lowest first (`32, 34`), sorts by the lowest, and is as wide as its widest text up to four
  slots; a longer list ends in an ellipsis. A click on a
  column header sorts by that column, a second click reverses the order; the headings always
  come first; Name ascending to start.
- The count line `x of y item(s) selected` sits under the list, ending in `Add from
  Wardrobe...` (the Load page; docs/outfit-management-ui.md §2). Live Preview follows it.
- Buttons: `Create / Update Outfit` (needs a ticked piece; opens the Save page), `Apply` (needs a ticked piece),
  `Exit`. `Restore` at the right end of the button row: a borderless text
  button lit on hover, with a `(?)`, shown only while Livery dresses the actor. It ends
  Livery's dressing of them. Its `(?)`: "Takes Livery's outfit off #{Name} and forgets their
  situational outfits. Items moved from your inventory stay with them."
- `Assignments::Choices` exposes the armor FormIDs of the pieces Livery has on them, which
  the headings are built from.

Checks: headings and columns on an NPC Livery dresses in a library outfit, on one it dresses in
loose pieces, on an undressed NPC and on the player; Restore on a dressed NPC; sorting.

## 2. Settings

INI + SKSE Menu Framework, nothing in the window.

| Key | Default | Meaning |
| --- | --- | --- |
| `[Window] ShowPlayerInventory` | 1 | NPC target: list the player's armor under heading 5 |
| `[Window] ShowPlayerWornPieces` | 0 | ...including what the player wears |
| `[Transfer] MoveFromPlayer` | 1 | Apply moves the player's instance to the NPC (§3); off: a plain copy |
| `[Transfer] ProtectFavorites` | 1 | favorites are left out of heading 5, as quest items always are |

## 3. Transfer

- At Apply only, for ticked pieces under heading 5: `TESObjectREFR::RemoveItem(armor, 1,
  ITEM_REMOVE_REASON::kStoreInContainer, extraList, npc)` moves the instance with its extra
  data (the container menu's path). The instance is chosen as Dress does: enchanted, else the
  most tempered, else plain. Then Dress finds it in the NPC's inventory.
- Quest items (`InventoryEntryData::IsQuestObject`, per form) and, with ProtectFavorites,
  favorites (`IsFavorited`) are not listed. Heading 5's `(?)` says so.
- The NPC's inventory is read again after the moves. A piece that did not move is dropped
  from Dress's list and counted in the HUD message; never a silent copy.
- A moved instance is not `added`: Restore leaves it with the NPC, as a traded item.
- The preview and Create / Update Outfit move nothing; Create and Apply moves as Apply does.

Checks: an enchanted piece moves with its enchantment; a quest item and a favorite are not
listed; the HUD counts moved pieces; the NPC keeps the piece after Restore.

## 4. Save page

Reached from `Create / Update Outfit` with the ticked pieces, which both tabs save. The page
writes the library; only Create and Apply gives the outfit a row. Giving an outfit to a
situation is the Situations page's job (docs/situation-management-ui.md).

- Heading: `Create New Outfit / Update Existing Outfit` in place of the title, in the
  heading font, where the title goes (Nordic: centred, over its rule), separated by a greyed
  `/`, text only (no box), the unselected one half transparent (`Theme::BeginHeadingTabs`);
  no actor name or summary line. The page is widened when the tabs would not fit on one
  line. Each tab keeps its name while the window is open, the Update tab its outfit.
- Both tabs start with the name, as the Wardrobe page's field: no label (`Outfit Name` on hover
  while not being typed in; a failed save's error instead).
- `Create New Outfit`: the icon field and the name, as the Wardrobe page's (its `Choose icon`
  popup, docs/outfit-management-ui.md §4), the name starting from the first free `New Outfit
  NNN`, then the ticked pieces read only, with the Dress page's icons, under their count
  line, as the Wardrobe page shows an outfit's. Buttons `Create`, `Create and Apply`, `Back`; Enter is Create.
- The icon follows the ticks until the user presses OK in `Choose icon`, which keeps it while
  the window is open (`OutfitIcons::SuggestKey`): the icon of the first ticked piece on the
  body slot, then head (head, hair, long hair), circlet, hands, feet, amulet, ring, then any,
  whose key `OutfitIcons::kKeys` lists and whose PNG is loaded; else the default key. Its
  colour is white (`#eaeaea`). Create writes it with the outfit; the Update tab keeps the
  outfit's icon and shows it before the name, as the Wardrobe page's icon field without its
  click, hover fill or tooltip (the default icon, greyed with the name, while none is chosen).
- Create and Apply saves as Create does and gives the new outfit the row the Wardrobe page's
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
  ITEMS` (the chosen outfit's pieces as the library has them, its count line counting those
  of unloaded plugins; `Select an outfit to update` while none is chosen) and `NEW ITEMS`
  (the ticked pieces), each piece with the Dress page's icon. Each column's list grows with its rows up to `kMaxListHeightRatio`,
  then scrolls. The tab opens with its outfit, or else the one last loaded, or else the
  library outfit Livery has on the actor, chosen. A click on an outfit chooses it, fills the
  name from it and shows it on the preview; a second click unchooses it, which greys the name
  and shows the ticks. Choosing the outfit already chosen keeps the name typed. Create and
  Back show the ticks again. Buttons `Update #{Outfit}` (`Update`,
  greyed with `kNeedOutfit`, while none is chosen) and `Back`; Escape clears the search
  first.
- The name may be changed: a name must be unused, ASCII case ignored, by any other outfit
  (CLAUDE.md §2.1); `Library::Update` takes the name.
- Update's confirm popup (`Replace items?`): `Replace the N item(s) of "A"` / `with the M
  ticked item(s)?`, a line for the pieces of unloaded plugins that stay in it and one for a
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
- When the outfit worn now is a library outfit X (the default's id when the default is worn,
  else the worn situation's) and the ticks are all or some of its loaded pieces and nothing
  else, Apply asks nothing: X goes to the row it is worn for in one `Assignments::Change`,
  the unticked pieces left off there (`OutfitRows::Exclusion`, as the Wardrobe page's Apply
  leaves them; none unticked wears all its items). The library does not change.
- Modal `Update "X" with the ticked items?` when the ticks hold a piece X does not. For a
  situation it adds `"X" is #{Name}'s Town outfit.`
  - `Yes`: `Library::Update(X, pieces = ticks, name kept)`. Default
    worn: then `Assign(ticks, X)`. Situation worn: the default is not touched; a forced
    redress puts the updated outfit on (`Assignments::Refresh`; `WearFor` only redresses on a
    change).
  - `No, apply as is`: `Assign(ticks, 0)`: the ticks become the Default's pieces, `chosen`
    0, random off. Its `(?)`: the pieces become #{Name}'s Default as they are, without a
    saved outfit, so editing the library does not change them; when a situation row holds an
    outfit, it adds that while a situational outfit is worn the new Default shows only when
    that situation ends.
  - `Cancel`: back to the window.
- Apply is greyed with no ticked piece (`kNeedPiece`).

Checks: no modal on a dressed NPC with one piece unticked, which comes off and stays unticked
on the Wardrobe page; the modal with a piece of another outfit ticked; Yes while a Town outfit is worn updates that outfit and the NPC wears it at once; No
in a town changes nothing visible until the NPC leaves.
