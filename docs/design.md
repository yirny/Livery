# Livery: design

Written 2026-10-19 for the AI and the developer who implement it. Current spec only;
the decisions below were settled with the user and verified where marked. The product
is Livery - Outfit Manager (`livery.dll`, `Livery.ini`, `Livery.log`,
`Data/SKSE/Plugins/Livery/`). The dressing core is `src/Dressing.cpp`, the
assignments and co-save `src/Assignments.cpp`, the library `src/Library.cpp`, the trade-menu
filter `src/TradeFilter.cpp`, the situations and their poll `src/Situations.cpp`.

## 1. Goals, in priority order

1. Simplicity and few bugs: the smallest design that works, engine mechanisms over hooks.
2. Player control: the player decides what an NPC wears; nothing is forced on every frame
   or every equip (no Skyrim Outfit Equipment System NG style re-application).
3. Removing the mod can never break a save. No ESP, no Papyrus, no DOFT writes: the
   `.ess` only ever holds engine-native inventory state. The SKSE co-save record is
   skipped when the plugin is absent.

## 2. How an NPC is dressed

An outfit is a list of armor base forms. Dressing an actor in it:

1. For each piece, pick an instance already in the actor's inventory, in this order:
   enchanted copy, highest-tempered copy, worn copy, plain copy. Player enchantments
   (`ExtraEnchantment`) and tempering (`ExtraHealth`) live on the instance, so the
   player's own gear keeps them. Equal ranks keep the worn copy, so a swap happens once
   and then settles. A swap takes the worn copy off before the better one goes on, and
   the worn copy goes back on if the better one does not; a worn copy another mod locked
   stays. An unworn instance that carries `ExtraCannotWear` cannot be equipped and is
   skipped.
2. When the actor has none, add a plain copy (`AddObjectToContainer`), give it an
   `ExtraUniqueID` (`InventoryChanges::GetNextUniqueID`) and record the key in the
   co-save (§5). Copies that stay are made only here, at assignment time, and when a
   piece taken in a menu is replaced (Keeping the outfit on); the live preview's own
   copies go back when it ends.
3. Equip each chosen instance with `ActorEquipManager::EquipObject(..., a_forceEquip =
   true)`. `forceEquip` is the game's PreventRemoval flag: the engine puts
   `ExtraCannotWear` on the worn instance and its best-gear pass (`UpdateWornGear`)
   skips that slot, so better armor the player gives later stays in the inventory, as
   long as the NPC's own outfit items do not take the piece off first (Keeping the outfit
   on). A piece already worn gets the flag added directly (`ExtraDataList::Add(new
   RE::ExtraCannotWear())` + `AddChange(kInventory)`), Tailor's way for wigs.
4. Take off worn armor that is not part of the outfit, except non-playable armor and
   pieces another mod locked (a lock already on a worn piece is never claimed). Only
   when at least one piece went on: otherwise nothing is assigned and the actor keeps
   what they wear. Every copy worn before step 1 is tagged (`ExtraUniqueID`) first; the
   ones that are off at the end, taken off here or by the engine for a slot a piece
   filled, are recorded as the actor's own pieces (§5) and go back on at release, unless
   a better copy of the same armor replaced them in step 1. An
   earlier assignment of the same actor is released first, its own pieces back on, so
   they are what the new one records.

A library outfit given to a row is dressed less the items it leaves off on that row
(`OutfitRows::Exclusion`, per actor, row and outfit): those unticked in its piece list on
the Wardrobe page when Apply gave it the row, on the Situations page in its details opened
from the row, or on the Dress page when its Apply dressed the actor in some of the
outfit they wore (below). The outfit's other items, those added to it later included, are worn; when
the items left off are all it has, it is dressed whole. The library outfit itself never
changes for this.

Slots the outfit leaves empty, those of the items it leaves off on the row included, are
governed by `[General] UnusedSlots` (in game `Gear in unused slots`): `All` leaves them the
engine's, `ExceptHeadgear` (the default) keeps headgear out of them (armor in any of body
slots 30, 31, 41, 42: helmets, hoods, circlets), `OutfitOnly` keeps them empty. The engine's
best-gear pass can put armor they own there (a helmet given in the trade menu, a hat a
dressing just took off); armor the setting keeps out comes off again right after each run of
that pass where the own-outfit guard is in (while the window's Live Preview shows them, as the
preview takes off what it does not show), at once while a trade menu is open, as it closes
and when their 3D loads (`Dressing::TakeOffExtras`, Keeping the outfit on). Armor another mod puts on them by script
stays on until the next of those. A change of the setting takes effect at the next of those. Whatever the setting, an NPC Livery has pieces on
never wears an outfit item that is not one of them (any `ExtraOutfitItem`: their own outfit,
or another mod's distributed one; the trade menu hides them): the own-outfit guard keeps the
worn-gear pass from putting them on, their unworn outfit copies carry Livery's lock so the
pass's best-armor step does not put them on either, and `Dressing::TakeOffExtras` takes off
any worn anyway, in any slot (Keeping the outfit on). Without the lock that step put an outfit
item on over armor given to them that is the same armor or weaker (2026-11-05, log), and one
the take-off had just taken off again at the pass's next run (2026-11-10, log); with it, armor
given to them for a slot where other armor may stay is what the step puts on. An actor in
their own clothes (no pieces) keeps every slot the engine's, their outfit items included.

Pieces from the player's inventory (the window's `Player's Inventory`) move to an NPC
at Apply, before the dressing, when `[Transfer] MoveFromPlayer` is on
(`Dressing::MoveFromPlayer`): `TESObjectREFR::RemoveItem(armor, 1, kStoreInContainer,
extraList, npc)`, the container menu's path, so the instance keeps its extra data. The copy
given is an unworn one ranked as step 1 ranks them, the worn one only with
`ShowPlayerWornPieces` on and no other. The NPC's inventory is read again after the moves;
a piece that did not arrive is left out of that dressing and counted on the HUD, never
replaced by a copy. A moved instance is the NPC's own: step 1 picks it, it is not recorded
as added, and Unassign leaves it with them. Quest items (`InventoryEntryData::IsQuestObject`)
are never offered, favorites (`IsFavorited`) not while `ProtectFavorites` is on. The
preview and the Save page move nothing, but Create and Apply, which moves them as Apply does.

The outfit the window's Apply gives (`Assignments::Assign`) is the actor's default. An outfit chosen for a situation (§7)
replaces it while the situation lasts, by the same steps: the worn pieces are released
(copies removed, the actor's own pieces back on) and the new outfit is dressed. When the
actor wears a library outfit and the ticks are all or some of its items and nothing else,
Apply gives it the row it is worn for (`Assignments::Change`), the unticked ones left off
there, as the Wardrobe page's Apply does, without asking and without a library write. Ticks
that are exactly the outfit last loaded (Load, or the Save page's Create) give the Default
that outfit without asking. When
the ticks hold an item it does not have otherwise, Apply asks first: Update updates
that outfit's pieces (`Library::Update`, name kept), then the Default
gets the outfit, or, for a situation outfit, `Assignments::Refresh` dresses the updated
outfit with the Default untouched; Apply Only makes the ticks the Default's pieces,
not a saved outfit, with `chosen` 0 and random off. The library write comes first; a
failed one moves and assigns nothing.

Never through `TESNPC::defaultOutfit` (DOFT): `Actor::SetDefaultOutfit` writes an NPC_
change form into the save, a runtime 0xFF outfit does not survive a load,
`RemoveOutfitItems` strips the NPC's own copies, and it is per base record, not per
reference. The DOFT path also does not stop the better-armor swap (SPID's description
says so), so a lock would be needed anyway.

### Keeping the outfit on

- `ExtraCannotWear` is saved with the instance, so nothing is re-applied on load.
- `UpdateWornGear`, seen when an item reaches the NPC and as their 3D loads, starts by equipping
  their outfit items: their own, or another mod's distributed outfit (SPID marks its items with
  that outfit's id). The trade menu hides those, and one in the slot of a locked piece takes
  the piece off and clears its lock (§6), after which the pass's best-armor step fills the
  slot. `src/OwnOutfitGuard.cpp` hooks that first call (SPID's `24234+0x1C3` on SE,
  `418622+0x25E` on AE, found as in §3; the 1.5.97, 1.6.1170, 1.6.1179, 1.7.99 and 1.7.104
  tables hold the ID) and leaves it out for every NPC Livery has pieces on
  (`Assignments::IsDressedNpc`; an NPC in their own clothes whose situation rows wait is not
  one) and for the NPC the window's Live Preview shows, assigned or not
  (`OutfitPreview::IsPreviewedNpc`, an atomic id), so their outfit items stay off while they
  wear Livery's pieces or the preview's; the rest of the pass, its best-armor step included,
  runs as before. The pass is not always on the game thread (§6), so the hook only reads those
  two and queues an SKSE task that runs after the pass. While the window's Live Preview shows them, the preview dresses
  them in its pieces again (`OutfitPreview::Reapply`), which takes off what it does not show;
  else `Assignments::TakeOffAfterPass` takes off what that step put on them that Livery keeps
  off (`Dressing::TakeOffExtras`, below) and, when it took something off, locks their outfit
  copies that came since (below), rebuilding their model only while the game is paused
  or frozen (a menu, the window with Live Preview off): a running game's actor update shows
  the change, and a rebuild makes body-morph mods refit. When it took something off the NPC
  the open window dresses (not a Save only one), the window reads what they wear again
  (`CaptureWindow::SetOutfitsWhileIdle`, skipped while a change of the window's own runs).
  After Create and Apply dressed a
  follower in body armor alone, the hat the dressing took off and their own outfit boots were
  on again seconds later, with no dressing by Livery between (2026-11-09, log; §6 for the
  take-off). The hook writes nothing, so it leaves no
  residue: once they have no pieces (Restore Outfit, Release everyone, a row in their own
  clothes) and the preview is off, the call runs for them again. SPID replaces the same call at `kPostLoad` and never
  calls the original, so Livery's goes in at `kDataLoaded` and calls SPID's in turn. Skyrim VR,
  AE before 1.6.640 (SPID's oldest, older tables unchecked, and a missing ID is fatal) and a
  build where the signature is not found run without it; there `Dressing::TakeOffExtras`
  takes their outfit items off again at a trade or 3D load (below), and their pieces go back
  on at the next 3D load.
- Their outfit copies are locked while Livery has pieces on an NPC (`Dressing::LockOutfitCopies`):
  every copy of an outfit item they carry (any `ExtraOutfitItem`) that is unworn, unlocked and
  none of the pieces gets `ExtraCannotWear` and a tag, and its key goes into the co-save (§5),
  so the best-armor step leaves it in the inventory (§6) and the pass needs no take-off. They
  are locked after each dressing that puts pieces on, and the copies that came since (another
  mod's outfit change, a copy the take-off just took off) after each 3D load, menu close and
  take-off after the pass that took something off. Livery takes those locks off
  (`Dressing::UnlockOutfitCopies`) before anything puts their own copies on, since a locked
  unworn copy cannot be equipped: at each dressing (before their own pieces go back on), at
  Restore, at death, at Release everyone, and for the live preview (`SuspendLocks`; locked
  again at its end). A lock already on a copy is another mod's and stays. A locked copy the
  engine marks worn comes off: `Dressing::TakeOffExtras` takes the lock off first. Never the
  player.
- An NPC who carries none of the pieces Livery put on them any more (an outfit change, an
  inventory reset, a script that took their items) gets their own clothes back, as Restore
  leaves them (`Assignments::ReturnIfPiecesGone`): the outfit copies are unlocked, their own
  pieces Livery took off go back on, then each outfit item they carry fills a slot no worn
  armor takes (`Dressing::PutOnOutfitItems`), and the pieces are forgotten. The record keeps
  their rows, so a situation row dresses them again at their next change of situation; with
  none it ends, and the next Apply dresses them. Checked at their 3D load, as a trade or
  container menu closes (after the replenish, below), and at each take-off check after the
  worn-gear pass, not while such a menu lists them: a follower going through a door with the
  player need not reload their 3D (§6).
- On `TESObjectLoadedEvent` for a managed NPC, after that check: if the instance chosen for a piece is
  still in the inventory but not worn (another mod took it off; taking off clears the
  lock), equip it again with the lock. Pieces whose instance is gone are left alone. No
  copies are created here. Then worn armor that is none of the pieces and is an outfit item
  (in any slot), shares a body slot with a worn piece, or takes a slot no piece has, is taken
  off (`Dressing::TakeOffExtras`; non-playable armor and another mod's locked pieces stay, and
  so does other armor only in the slots of pieces gone): fast travel can mark the NPC's own outfit
  items worn again beside the locked pieces (§6), and the best-gear pass fills a slot the
  outfit leaves empty. The sink only checks a mutex-guarded id list
  and queues an SKSE task, since the event's thread is not documented.
- A piece the player takes from an assigned NPC in the trade, container or pickpocket
  menu goes on again as that menu closes (`Dressing::Replenish`): a copy of that armor
  they still carry, else a new plain copy, recorded as added (hidden in trade, taken back
  at release). Only pieces that were the actor's or the player's: a copy Livery added
  that is taken (where §3's filter does not hide it) stays gone, so taking it again makes
  no new copy. The filter's list fill notes the NPC (`Assignments::NoteShownInMenu`, an
  atomic id); a `MenuOpenCloseEvent` sink takes the note when `ContainerMenu` or
  `BarterMenu` closes and queues an SKSE task; other menus return after the name compares.
  The note is not cleared at opening, since whether the list fills before that event is
  not documented; only those two menus list an NPC's inventory. While one of them is open
  the sink keeps a `TESEquipEvent` sink registered (added as it opens, removed as it
  closes, so normal play sends it nothing): armor an assigned NPC puts on then (the
  best-gear pass at a give) queues an SKSE task that takes the armor outside the pieces off
  at once (`Dressing::TakeOffExtras`) and rebuilds their model (`Dressing::RefreshModel`),
  since the menu pauses the game and with it the actor update that would show the change.
  A piece still missing (refused, a runtime where the
  filter found no call site, another mod's inventory access) leaves the slot to the
  vanilla picker until the player assigns again. After that replacing, the same task
  swaps each worn piece for a better copy the NPC now carries (`Dressing::Upgrade`, step 1's
  ranks): the piece's lock comes off, the better copy goes on locked and the piece names
  it, and a replaced copy Livery added is taken back; a piece another mod locked stays.
  Then the armor the engine put on them outside the pieces comes off, as on a 3D load
  (`Dressing::TakeOffExtras`): a helmet given for a slot the outfit leaves empty.
  `GiftMenu` is not one of these menus, so a better copy given there waits for the next
  dressing; armor the pass puts on for a gift comes off after it (`UpdateWornGear`, above).
  The window counts the pieces Livery
  put on that the actor wears no copy of, and the worn armor it did not put on (`GapsOf` in `Plugin.cpp`, read as it opens
  and after each dressing with it open, no preview on): the summary line then says the
  outfit is only assigned, `Assigned X as Default, 1 item not worn.`, an extra piece
  is added as `, 1 extra item worn`, and the Wardrobe page's Apply is not greyed as worn
  already; it dresses them again, a copy added for a piece they no longer carry (§2 step 2),
  the extra pieces taken off (step 4).
- Unassign (the window's Restore): release the mod's locks, those on their outfit copies
  included, remove the mod's copies
  still present (by co-save key; never a player-given instance), put the actor's own
  pieces it took off back on (those still carried, unworn and unlocked, without a lock),
  forget the actor. A slot whose own piece is gone is left to the engine's next
  re-evaluation, which can be a cell change or two later.
- Death: release the mod's locks (`TESDeathEvent`) and forget the actor, so the corpse
  behaves like vanilla; the copies stay on it, lootable, and its own pieces are not put
  back on.
- Release everything (for uninstall): Release everyone at the end of the SKSE Menu
  Framework settings page, confirmed in a popup, refused without a loaded save or while
  the capture window is open; no key does it. Without SKSE Menu Framework, `[Uninstall]
  ReleaseEveryone=1` in the INI (read once at start) queues the same release as an SKSE
  task at every `kPostLoadGame` (not `kNewGame`), after a HUD message that the mode is on;
  Livery never resets the key. Unassign every managed actor. Actors not
  in memory (non-persistent NPCs whose cell is not loaded) cannot be reached and keep
  their locks and copies, their outfit copies locked too: with Livery removed, such an NPC
  who loses Livery's pieces cannot put those copies on until their cell resets. Locks and
  copies are otherwise the only residue after removal; they do not break a save.

### The player

Same dressing path, three differences: no lock (a locked piece cannot be taken off in
the inventory menu, verified); adding pieces the player lacks is OFF by default,
switchable in the settings (`[Player] AddMissingPieces`) with a warning that it creates
items; nothing is put back on the player when they load. Situation outfits (§7) switch
on the player as on an NPC, unlocked like the default, and not in beast form.

### Who the hotkey opens on

With nothing under the crosshair, or anything but a person (`TESObjectREFR::IsHumanoid`, the
`ActorTypeNPC` keyword: not a horse or a creature), the hotkey opens the window on the player,
always Full management. An NPC falls in the first group that fits, read at the
hotkey press (`GroupOf` in `src/Plugin.cpp`): followers and spouse (teammate flag or
`PlayerMarriedFaction`), friends (`BGSRelationship::GetRelationship` of `Actor::GetTemplateBase`
and the player's base at Friend or better; no record is Acquaintance), other NPCs. Relationship
data is not stored for templated actors and what scripts set on one is gone when the game
session ends (CK wiki, GetRelationshipRank), so such an NPC may count as other NPCs after a
restart. Each group has an access in `[General]` (`Settings::AccessOf`), defaults in brackets:
`FollowersAndSpouse` (`Full`), `Friends` (`SaveOnly`), `OtherNPCs` (`Off`). Children are refused first.

- `Full`: the whole window. `Off`: a HUD message, no window.
- `SaveOnly` (`Request::saveOnly`): the window lists only the worn armor with its ticks fixed,
  has the Dress and Appearance tabs, no Apply, Restore, Create and Apply or link to the
  Wardrobe, and writes the library only through the Save page. Its Live Preview only keeps
  the game running and never calls `OutfitPreview`, so the inventory stays untouched. An Update of the
  outfit the NPC wears still redresses them, as for every wearer of an updated outfit.

The group is read only there: outfits an NPC already wears stay on when their group changes,
and a Save only NPC in a Livery outfit is released by Full management or Release everyone.

### Items

Livery deletes an item in two places only, each by its own `ExtraUniqueID` key and only for a
copy Livery added: `Dressing::Release` with `removeCopies` (Unassign, a redress, release everything) and the live
preview's take-back of the copies it added. An actor's own instance is never deleted: the pieces
Livery takes off stay in the inventory, and death releases the locks and removes nothing. The
one removal without an instance key is the rollback of a refused equip (`PutOn` in
`Dressing.cpp`), one copy of the armor just added with `AddObjectToContainer`; it runs only when
the actor had no unlocked copy, so it can reach the actor's own copy only when that copy is
unworn and locked by another mod. `MoveFromPlayer` moves the player's instance to the NPC, who
owns it from then on: lootable at their death, gone with a respawned NPC.

Copies Livery adds to an NPC reach the player only by looting the corpse; through the trade,
container or pickpocket menu of an NPC whose base has no `defaultOutfit`, or on a runtime where
the filter found no call site (§3); through other mods' inventory access (follower frameworks,
Papyrus `RemoveAllItems`), which the menu filter does not see; or from an actor out of reach at
release everything, whose copies are ordinary items once Livery is removed. Copies on the player exist only with
`[Player] AddMissingPieces` on: unlocked ordinary items, and Release takes back only those still
carried. The capture menu swallows every input event and disables the pause menu, so no save is
made while a preview is on, and no preview copy reaches a save. While the window is open the
HUD is hidden and an NPC is held where they stand (`src/MovementHold.cpp`); the game's menus
are shown even if the console's `tm` hid them, since the window is drawn and reads its input
only while they are, and hidden again as it closes.

A dressing's HUD message names the pieces not carried (the player without `AddMissingPieces`)
and those the engine refused to put on (`DressingNotes` in `src/Assignments.cpp`).

### Live Preview camera

While the window is open the actor is seen through sarrai-common's free camera, Live Preview
on or off (on, the game runs; off, it is paused). It starts where the view was, so an NPC's
view does not change as the window opens; on the player it glides from SmoothCam's shoulder
offset until they stand in the middle. The window's close puts the view back. With the cursor over the game,
not the window: W/A/S/D move (Shift for half speed; ignored while a text field is active),
a left drag orbits the actor sideways and tilts up and down, a right drag pans, the wheel
zooms; `[Camera] SpeedPercent` scales all four. The camera stays near where the actor stood
when the window opened: it stops with them at the edge of the view, and just short of
passing through them.

### Contracts

Building calls and anything taking `RE::Actor*` run on the game thread (SKSE task).
Raw inventory pointers are read again after every native call; an instance is found
again only by its `ExtraUniqueID` key. `InitInventoryIfRequired` is never called before
worn state is read: on an NPC who wears outfit items it takes `ExtraWorn` off every
outfit copy (§6).

## 3. Hiding the mod's copies in the trade menu (stage 3)

Copies the mod adds are ordinary items, so the trade, container, pickpocket and gift menus
would list them and the player could take them for free. The engine hides an NPC's own outfit items in
those menus by one rule, applied inside the functions that fill a menu's item list from an
inventory: an entry whose `ExtraOutfitItem` names the actor base's current `defaultOutfit` is
skipped. `src/TradeFilter.cpp` hooks the calls to those functions, the ones SPID hooks for
its distributed outfits (`SPID/src/Outfits/OutfitManager+Hooks.cpp`, `FilterInventoryItems*`;
SPID swaps `defaultOutfit` around the call). Around the original call, the copies Livery added
to an assigned NPC (the co-save's `added` keys) carry a temporary `ExtraOutfitItem` with the
NPC's `defaultOutfit` (`Dressing::MarkAsOutfitItems`); it is taken off again before the call
returns, so no `SetOutfit` can ever see it and nothing is flagged as changed. Only the mod's
copies are hidden: player-given instances stay visible and takeable even while they are part
of the outfit (taking back one's own item is not a dupe; the NPC gets a copy of it as the menu
closes, §2 Keeping the outfit on, which leaves them as an Apply without the gift would). Not filtered: the player's own list
(a copy added to the player is the player's item), an NPC without a default outfit, and
corpses (Livery forgets an actor at death, and vanilla shows a corpse's outfit items too).
The capture window's `Inventory` heading follows the engine's rule: an NPC's own outfit
copies are left out while unworn (`Plugin.cpp`, `CollectArmor`), so the list shows what the
trade menu shows; a worn piece Livery did not put on is listed there too unless its worn copy
is an outfit item (`ExtraOutfitItem` of any outfit), which goes under `Outfit (Unmanaged)`.

Call sites, Address Library IDs (the 1.6.1170, 1.6.1179 and 1.7.104 tables hold them all, the
overlay's callees in sarrai-common's `src/Overlay.cpp` included), with SPID's offsets: AE from 1.6.1130, 1.7.x included,
`51145+0xEB`, `51146+0x15B`, `51144+0x10F` (an AE function inlines the caller of the first); AE
up to 1.6.629 `51145+0xBE`, `51146+0x12E`; AE 1.6.640 `51144+0xE2`; SE 1.5.97 `50216+0xED`,
`50217+0x152`. The Steam executable is DRM-wrapped, so the bytes are checked at install instead:
a site is hooked where SPID's signature, the `call` and the instructions after it
(`E8 ?? ?? ?? ?? 90 48 85 DB` for the list fill), matches, at SPID's offset or else at the one
place in the function's first `kScanBytes` (0x400) where it does, which the log reports; a
signature found nowhere, or twice, leaves the site alone with a warning. Skyrim VR is not
covered. Checked in game on 1.6.1170 only; the overlay's own two hooks (sarrai-common, `src/Overlay.cpp`) check
at install that their sites hold a `call` reaching the callee dMenu NG verified on 1.6.1170,
1.7.99 and 1.7.104, or code outside the game's `.text` (another plugin hooked the call first, as
HotkeyAtlas does the input dispatch; Livery's hook then calls that one in turn, and the log says
so), and leave the overlay off otherwise.

Rejected: a permanent `ExtraOutfitItem` on the copies (id = the NPC's DOFT hides them in
vanilla, but any `SetOutfit` then deletes them, and a non-outfit id has no precedent);
removing the entries from the filled `ItemList` (its GFx array and its engine-heap `Item`s
would have to be kept in step).

## 4. Data

- Outfit library: own JSON under `Data/SKSE/Plugins/Livery/`, shared by every save.
  Armor identity = local FormID + plugin file name, resolved with
  `TESDataHandler::LookupForm` (ESL ids are 12-bit), as Tailor's `outfits.json`. A file that
  cannot be read leaves the library empty and is never written; `Livery.log` says why
  (`Documents/My Games/Skyrim Special Edition/SKSE/`, `Skyrim Special Edition GOG` for GOG).
- An outfit has no categories and no gender (the window calls the library the wardrobe).
  Other fields are ignored and gone at the next write. Situations are per actor (§7), not in
  the library.
- An outfit has an icon: `"icon"`, PieceIcons' key of a PNG under `icons/`
  (`skyui/icons_item_psychosteve/armor_body`), and `"color"`, the key of its tint in
  `OutfitIcons::kColors` (`glass-teal-light`), so a tint changed there shows on every outfit
  that has it. A missing or unreadable one, or a colour key the table does not have, is the
  default (`OutfitIcons::kDefaultKey`, `bone-white`), so a file edited by hand still reads; `Library::Create` takes the Save page's Create tab's icon.
- `Library::Update` replaces pieces, name and icon; a name is taken when another outfit
  has it (ASCII case ignored). The Save page's Update keeps the icon.
- `Library::Remove` takes an outfit out; ids are never reused. `Assignments::ForgetOutfit`
  then takes it off every row in this save (a `chosen` that was it becomes 0), and a Default
  this leaves wearing nothing gets the outfit's pieces, read before the remove, as its
  pieces. Other saves' rows naming it keep the id, which counts as not on the row and never
  shows (docs/situation-management-ui.md §1); restoring outfits.json brings it back.
- Situation data: `situations.json` beside the library (`src/SituationStore.cpp`; the
  presets, the file's parse and write and the name rules in `src/SituationTable.h`): per
  situation key its name and removed flag, only for the situations that
  differ from the presets; a missing or empty file is every preset as installed and is not
  written at load. The same load and write rules as the library (docs/situation-management-ui.md
  §2); an empty `outfits.json` is an empty library too. Loaded at `kDataLoaded`, before the library and any co-save load.
- Chests (docs/chest-ui.md): `chests.json` beside the library (`src/Chests.cpp`), named sets of
  items that are never worn, several on one body slot too, offered when dressing; a chest may
  be empty. The pieces are stored as the library's (`src/PieceFile.h`, shared), with the same
  load and write rules; loaded at `kDataLoaded` after the library, whether or not the
  `[Window] ItemChests` setting (off by default) shows the window's Chests tab. Never named by
  the co-save.
- Settings: `Data/SKSE/Plugins/Livery.ini`, written with defaults at first start; keys a newer
  version adds are appended to an existing file, so an update never resets it. Read once at
  start through the profile API; written only by Livery's own file I/O, which edits the file as
  the profile API would, in the file's encoding as the API reads it (UTF-16 LE after its byte
  order mark, else the ANSI code page), and replaces it through a temporary file and a rename,
  never `WritePrivateProfileString`: INI cache plugins such as PrivateProfileRedirector hold those
  writes in memory and drop them when they reload the file. A setting is written as it changes,
  the lists' sorts as the window closes (lost if the game crashes first).
  The SKSE Menu Framework page shows the same settings and applies a change at once;
  its Restore default settings, after a popup, resets every one but the palette. README.md's
  Settings table lists the keys.

## 5. Co-save

Co-save id `LVRY`, records `ASGN` (version 6) and `EXCL` (version 2), read in the SKSE load
callback, cleared on revert; a record of another version is skipped with a log line. A
situation is stored by its key (`Situations::Name`, the key `situations.json` uses): a
length byte and its characters, so situations can be added or removed without a new
version. A key the version does not have drops its row with a log line, or reads a row worn
as the Default.

- Per assigned actor, the player included (FormID, fixed with `ResolveFormID`): the
  library outfit the pieces on them are from (`wornOutfit`, 0 for none or the Default's
  pieces), the pieces Livery has on them, each as its instance key `(armor FormID,
  ExtraUniqueID baseID, uniqueID)` and two flags: Livery added the copy, Livery set its
  lock. NPCs are locked, the player never, so no per-actor lock flag is stored. Then the
  instance keys of their own copies Livery took off (§2 step 4), and those of their outfit
  copies Livery locked unworn (§2 Keeping the outfit on), each a count and the keys.
- The Default's pieces: armor FormIDs not in the wardrobe, worn while the Default's row
  wears no outfit (docs/situation-management-ui.md §1). Then the situation of the row the
  pieces are for, and the rows (`OutfitRows`): a count, and per row its situation, random,
  `chosen` and its library outfit ids. Every row
  reads its outfit from the library each time it goes on; the window names the outfits
  from the ids. A row for a situation the version does not know is dropped with a log line.
- A piece, own copy, locked outfit copy or Default piece whose plugin is gone is dropped at load, and an
  assignment with nothing on the actor, no row (the Default's included) holding an outfit and
  no row random (`IsEmpty`; a row holding an outfit none of whose items is loaded keeps it). Before that
  check, the row of a situation removed in `situations.json` (§4) is dropped, with a log
  line; pieces worn for it stay on until the poll's next dressing. A key whose
  instance is gone is skipped whenever the actor is dressed or released.
- `EXCL`, written after `ASGN` while any row's outfit leaves items off (§2): a count, and
  per entry the actor FormID, the row's situation, the library outfit id and the armor
  FormIDs it leaves off. A record of its own, so a Livery that does not know `EXCL` skips
  it and dresses every item. At load, after `ASGN`, an
  entry for an actor without an assignment, a row the version does not know or an outfit
  no longer on that row is dropped, and so is an armor form whose plugin is gone.

Per character and per save by construction. An export/import of assignment sets (JSON)
can come later if sharing across characters is wanted.

## 6. Verified facts (2026-10-19, in game, runtime 1.6.1170)

- `ExtraCannotWear` on an NPC's worn pieces, their own outfit items: better armor given in
  trade is not put on; survives save/load and fast travel.
- `EquipObject(..., forceEquip = true)` on a fresh copy: worn at once, and the engine
  added `ExtraCannotWear` itself. Equipping a copy of the same armor the NPC wears
  worked first time, unlike the DOFT preview path.
- Removing the flag does not trigger a re-evaluation; the NPC changed at the second cell
  change after a release.
- The player cannot unequip a locked piece.
- A locked piece the NPC owns as a normal item can be taken in the trade menu; DOFT
  outfit copies are hidden by the engine only when their `ExtraOutfitItem` id is the
  current DOFT (SPID's filter handler shows the rule).
- `TESObjectREFR::InitInventoryIfRequired` on an NPC wearing outfit items (Whiterun
  guards, a follower) left every outfit copy without `ExtraWorn` (one copy per armor,
  `ExtraOutfitItem` kept, the 3D unchanged); a non-outfit copy kept it.
- Fast travel (two in a row, 2026-10-29) left a follower in Livery's locked outfit with
  their own outfit items (body, feet) marked worn again beside the locked pieces of the
  same slots: the window listed both, the 3D showed only Livery's, and the trade menu
  showed neither (Livery's copies filtered, their own outfit items hidden by the
  engine). A load door and waiting an hour did not do it; the next dressing took them off.
- Armor given in the trade menu with Skyrim Souls keeping time running (2026-10-31, log, SPID
  7.3.3 installed, no own-outfit guard yet): a follower in three locked pieces, with their own
  outfit's body and feet items unworn in the inventory. At the give those items went on, which
  took the locked cuirass and boots off and cleared their locks; then the given cuirass and the
  outfit's boots went on as the best armor, unlocked. The gauntlets, in a slot their own outfit
  does not hold, stayed locked. With their own outfit items as the worn locked pieces, only the
  weapon was equipped in the trade menu and the locks stayed. As their 3D loaded those items went
  on again and read unlocked; `Dressing::PutBackOn` locked them again.
- The own-outfit guard (2026-10-31, runtime 1.6.1170, SPID 7.3.3): `Own-outfit guard installed
  at UpdateWornGear+0x25e`, and the follower above kept the outfit on with better armor given
  in trade, the armor staying in the inventory, unworn; doors and fast travel changed nothing.
  In the log only the weapon was equipped at a give and at their 3D load, the three pieces
  stayed locked, and Restore put their own items back on. With a cuirass-only outfit, their own
  boots stayed off, after a cell change too.
- The take-off after the worn-gear pass (2026-11-10, runtime 1.6.1170): Create and Apply of a
  body-armor-only outfit on a follower carrying a hat, Live Preview off: the pass ran 26 ms
  after the dressing, in the frozen game, and the task took the hat and their outfit boots off
  (`Took off 2 item(s) ... after the game's worn-gear pass`). After the window's close the pass
  ran again 5 to 22 s later, each run putting one or two of them on and the task taking them
  off, never in a loop.
- A lock on an unworn outfit copy (2026-11-10, runtime 1.6.1170, a verification build): a
  follower's two outfit copies (body and boots), locked unworn right after the take-off, were
  never put on by the 7 passes that followed over 15 minutes (gives in the trade menu, Livery
  dressings, the live preview); they stayed unworn and locked. The pass stopped re-running
  every few seconds.
- The worn-gear pass's thread (same build): the hook ran once on another thread than the SKSE
  task it queued (likely a door's load), and afterwards on the same thread as its tasks. The
  hook cannot count on the game thread.
- A follower going through load doors with the player (2026-11-10, same build and the
  locks' build): three door changes showed no worn-gear pass on the follower, and with their
  only piece removed by console the 3D-load check did not run until the window's close; they
  stayed unclothed until then.
- An NPC's own outfit copy taken off and equipped again, the same instance in a later
  frame, goes back on (live preview).
- Situation outfits on a follower (§7): Town in a town and the default outside it; Cold
  outdoors by weather (a region that is not snowy, while cloudy weather still counted); the default
  during a fight and the situation outfit after it; the choices and the switching survive
  save and load; a version 2 co-save record is read.
- Clearing situation choices (§7): two cleared at once; Town cleared while in a town put
  the default back on at once; the cleared choices stayed cleared across save and load.
- Situation outfits on the player (§7): Town in Whiterun, Cold by weather, and the
  in-water condition when wading on foot (while a situation used it); no redress while a
  werewolf, and the right outfit on after turning back; wading on horseback is not in
  water; the choices survive save and load.
- Dressing while the capture window froze the game (Live Preview off, 2026-10-23):
  `EquipObject` and the assignment change the equipment data at once, so the window's list
  follows at once, but an NPC's new pieces get no 3D until their actor update, which the
  frozen game skips, while a copy taken back (`RemoveItem`) loses its 3D at once (the torso
  showed empty). `Set3DUpdateFlag(kModel)` + `Update3DModel` on the NPC right after the
  dressing shows the new outfit at once, in the frozen game.
- The trade-menu filter (§3) hides Livery's copies in the pickpocket menu too (2026-10-23):
  it fills its list through the hooked container-menu call sites.
- Sources for the PreventRemoval semantics: Tailor `src/wig/WigEquipment.cpp`
  (prevents removal when worn, equip when unworn), SPID `SPID/src/Outfits/Helpers.h`
  (blocks the slot in `UpdateWornGear`).

## 7. Situations

An assigned actor has a row per situation, Town, Castle, Home, Sleep, Cold
(`Situations::kChoosable`), and the Default, what the window's Apply gives, worn where
none of them applies. A row is a list of library outfit ids, a random flag and the one
chosen (docs/situation-management-ui.md §1, `src/OutfitRows.h`); the Default also keeps
pieces not in the wardrobe. Every row reads its outfit from the library each time it goes
on. Each situation's name comes from `situations.json` (§4): the window and the HUD show
it, the sentences that say what is worn end in ` for <name>` (the summary line in `as
Default` for the Default, the HUD in nothing), and the logs keep the key. A removed situation offers no row
and nothing wears it. Random off, the row wears `chosen`; random on, one of the list is picked as the row
goes on and kept until it changes, a forced redress keeping it while it is on the list,
and recorded as `chosen`, so turning random off keeps the last pick on. A click on the
die of the row worn on the Situations page picks again, never the outfit worn
(`Assignments::PickAnother`), and puts the pick on at once. Giving a row an
outfit (`OutfitRows::Give`) with random on puts it at the front of the list; with random
off it becomes `chosen` in the place of the outfit it replaces, which leaves the list, so
the list keeps its length. The Save page changes no row but by Create and Apply, which gives
the new outfit the row the Wardrobe page's Apply would (below) in one `Assignments::Change`, after
the library write, with one forced redress. The Wardrobe page's Save changes no row; its `Apply to #{Name}` changes
what the actor wears now: while they wear another outfit for a situation, its popup gives
the outfit that row (Yes, `OutfitRows::Give`, worn now) or the Default (Apply as Default,
the situation's outfit staying on); else the outfit goes to the situation they wear it
for, or the Default. While the player dresses from their inventory only
(`Request::carriedOnly`), that Apply first lists the ticked items they do not carry, before
its popup and any library write: OK goes on, greyed when they carry none of the ticked items;
Back changes nothing.
The Situations page (docs/situation-management-ui.md §3-§6) edits every row of the captured
actor by drag and drop, the random flags included, and the situations' names; each save,
0.5 s after the last edit or as the page is left, writes `situations.json` first, then replaces the rows
(`Assignments::SetRows`, the record only) and dresses them once (`Assignments::Refresh`,
at once, Live Preview on or off; a random row worn now wears the outfit it gained
first). Removing a situation writes `situations.json`, then clears the situation's rows
in this save (`Assignments::ForgetSituation`) and takes those wearing it out of it as Remove takes them out of a removed outfit; Restore Presets changes no row. An actor can have situation outfits without a default and then wears their
own clothes outside them. Restore forgets the choices with the rest. The
player has the same rows.

### Detection (`Situations::Evaluate`)

The situations an actor is in, the most specific first (`Situations::Applicable`); the
first one whose row holds an outfit is worn, else the Default (`OutfitRows::Resolve`).
Each situation applies while its condition holds (`Situations::Condition`,
`Situations::ConditionOf`); Evaluate checks the situations in `Situations::kPriority`
order, Sleep, Cold, Home, Castle, Town, and only their conditions. The rules are Tailor 3.0's
(`src/events/SituationHandler.cpp` and its policy headers). Verified in game in Livery
(§6): Town, Cold by weather (while cloudy weather counted), combat, in water by wading.
Not yet: Castle, Home, Sleep, Cold by a snowy region. In combat no situation applies, so
a fight is fought in the default outfit. The conditions:

- In water (no situation Livery installs uses it; kept for situations whose conditions
  can be chosen): the actor state's swimming flag, or the engine reports a water height
  relevant to them no lower than 40 units under their feet (wading counts); once the poll
  saw them in a situation in water, they stay in it until that water is 60 units under
  their feet, so a shore does not flip it on and off. On a mount only the swimming flag
  counts: a rider's feet are above the water the horse wades.
- Sleeping (Sleep): sit/sleep state `kWantToSleep` to `kWantToWake` (the other states are chairs
  and mounts), or on furniture flagged CanSleep. Vanilla beds never put the player into
  bed (Tailor's note in `SituationHandler.cpp`), so the player's Sleep needs a mod that does.
- Cold: in an exterior cell and the weather is rainy or snowy, not cloudy alone (while the sky
  blends weathers, the last one decides until the new one is halfway in), or the sky's
  weather region, the one around the player, has snow weathers for at least a quarter
  of its weather chance (its clear days are its own).
- Home: the current location has `LocTypePlayerHouse`, or `LocTypeHouse` for an NPC.
  Town follows it, so a home without a Home outfit wears the Town one.
- Castle: the current location has `LocTypeCastle`. In Skyrim.esm these are the jarls'
  seats but Winterhold's longhouse (a house), and Solitude's Castle Dour with its Emperor's
  Tower; the DLCs add none. Each is a dwelling too, so Town follows it.
- Town: `LocTypeCity`, `LocTypeTown`, `LocTypeSettlement`, `LocTypeDwelling` or
  `LocTypeInn`, or Home's. Keywords keep their editor IDs at runtime and are found by
  them; a location's own keywords only.

A situation is thus an ordered list of conditions; user-defined conditions, as Skyrim
Outfit Equipment System NG offers them, would extend this list.

### Switching

A thread (`std::jthread`, no hook) queues one SKSE task per second. The task does nothing
while the game is paused, the loading menu is up, the capture window is open or a preview
is on, since a redress would undo the preview's restore. For each actor with a situation
outfit, or still wearing one whose row a Remove cleared, who is 3D-loaded and alive, the player not in beast form (a race that is not
playable: werewolf, Vampire Lord; the transformation takes the gear off and puts it back
on), it evaluates the situations and, when they differ from the last poll's, calls `Assignments::WearFor`, which redresses only when the resolved
row or the outfit it wears differs from the one worn (`worn`, `wornOutfit` in the record). One redress per poll: when the
weather turns for everyone, the others follow at the next polls. The poll redresses an
actor at most once in 3 seconds; a change it holds back is not marked seen, so it is
evaluated again at a later poll. A change is acted on
once: a situation outfit that puts nothing on falls back to the default and is not tried
again until the situations change. With `[Situations] NotifyChanges` on (off by default),
each such redress shows a HUD message naming what they wear now. Apply and the Save page
dress for the current situations at once. The poll stops at `kPreLoadGame` and starts at `kPostLoadGame` and
`kNewGame`, forgetting what it last saw. The record's `pieces` describe whatever is worn,
so a situation outfit is kept on, locked and put back at 3D load like the default.

## 8. Plan

Built and checked in game: the dressing core (§2), the library and the capture window
(hotkey Shift + X by default), the trade-menu filter (§3), situation outfits for NPCs and
the player (§7), the window's pages (docs/ui-redesign.md, docs/outfit-management-ui.md,
docs/situation-management-ui.md) and the chests (docs/chest-ui.md, behind the `Enable chests`
setting, off by default). Open:

1. Later: user-defined conditions in the way of Skyrim Outfit Equipment System NG,
   extending `Situations::Applicable` (§7); layer outfits (docs/memo-layer-outfits.md).
2. The open in-game checks: Castle, Home, Sleep, Cold by weather without cloudy and by a
   snowy region (§7); quickload with the window open, then New Game; the player's left-hand
   weapon back after a shield preview; tempered player gear after Exit; a follower's given
   armor gone after Load Outfit + Exit.

## 9. Piece icons

- The piece list draws an icon left of each piece's name (`src/PieceIcons.cpp` picks it,
  `Theme::TickRow` draws it). An icon is a white PNG, square, any size, tinted on draw, at
  `Data/SKSE/Plugins/Livery/icons/<iconSource without .swf>/<iconLabel>.png`, matched ASCII
  case-insensitively. Every PNG there is loaded at the overlay's start with its halvings
  down to 1 pixel, and a row draws the smallest one still as wide as the icon on screen:
  ImGui's DX11 sampler reads only a view's top level, so each halving has its own view.
- A piece shows the icon the game's inventory shows for it with SkyUI and I4, whenever Livery
  may use that icon's art: Livery ships every icon of an icon swf whose art it has permission
  for, and reads SkyUI's rules and I4's as below.
- A piece starts with the icon SkyUI 5 gives its armor (`processArmorIcon`): the label by
  weight class and main slot (`lightarmor_*`, `armor_*`, `clothing_*`; `armor_amulet`,
  `armor_ring`, `armor_circlet`; else `default_armor`) under `skyui/icons_item_psychosteve`,
  tinted `#756000` for Light, `#6B7585` for Heavy and `#EDDA87` otherwise. SkyUI 6's cloak
  and backpack (slots 46 and 47, the main slot when it is the lowest an armor covers and none
  of SkyUI 5's named slots is covered) take `clothing_cloak` and `clothing_backpack` whatever
  their class, in their class's colour, and the Type `Cloak` and `Back`.
- Then the rules of Inventory Interface Information Injector (I4), read at `kDataLoaded`
  as I4 reads them: `SKSE/Plugins/InventoryInjector/<plugin>.json` for each active plugin,
  in load order, comments and a comma before a closing bracket allowed as I4 allows them.
  Each rule a piece matches sets the icon, the colour and the Type text
  (`subTypeDisplay`) it assigns, so a later rule wins field by field. Livery reads only the rules for `formType: "Armor"` whose
  match uses `keywords`, `formId` and `parts` alone; an array there is conditions that must
  all hold, `{"anyOf": [...]}` one met by any of its values. A rule's icon whose PNG is
  missing is ignored and the earlier icon stays; a rule without `iconSource` names
  SkyUI's swf.
- A piece's Type column shows the Type text of its rules in place of SkyUI's slot name, as
  SkyUI with I4 does; the column still sorts by slot. A `$` key is looked up in the game's
  translations (`Interface/Translations/<name>_<sLanguage>.txt`, through
  `SKSE::Translation::Translate`) when the window lists the pieces; a key with no
  translation is ignored and the earlier text stays, so a raw `$` key never shows.
- Keywords are tested by editor ID when the window lists the pieces, so those KID adds count.
- While the player dresses from their inventory only (`Request::carriedOnly`), every piece
  list draws the rows of `PieceGroup::kNotCarried` at half opacity, with the reason on hover.
  Their ticks and what they do stay as they are.
- The tick rows (the Dress page's and the Wardrobe page's) draw marks after a piece's name
  (`Theme::RowMark`, drawn in code), in this order: a lightning bolt in SkyUI's enchantment
  blue `#A2BEFF` on an enchanted piece, then a hammer in pale orange `#F2B880` on a tempered
  one (a hammer alone sits where the bolt would). Over the bolt alone they show the game's
  description of the enchantment (`MagicSystem::GetMagicItemDescription`); over the hammer
  alone, its quality as the game names the item: the text inside the name's last parentheses
  (`Fine` from `Iron Helmet (Fine)`), else the whole name, from an `ExtraTextDisplayData`
  built apart from the copy, so the inventory does not change. Both are read when the window
  lists the pieces.
- Both marks are those of the copy the row stands for, ranked as Dressing ranks copies
  (enchanted, then most tempered): of a piece the actor carries, the best copy, the worn one
  on a tie (`ExtraEnchantment`, `ExtraHealth`); under Player's Inventory, the copy the player
  would hand over, the best unworn one, the worn one only when it is the only copy. A copy
  without an enchantment of its own, and a piece nobody carries, have the armor's own
  (`formEnchanting`); a piece nobody carries is never tempered.
- Livery ships a PNG for every label of SkyUI's `icons_item_psychosteve.swf` (PsychoSteve's
  art, and Jelidity's SkyUI 6 additions) and of `I4IconAddon/icons.swf` (Jelidity's art),
  rendered from the swf frames as is, with their permission (`icons/ICON-CREDITS.txt`). Other
  icon packs' PNGs are not in the repository.
- Outfit icons use the same PNGs (§4: an outfit's `icon` and `color`). The outfit rows, their
  drag copies, the icon fields (the Wardrobe page's, the Create tab's, the Update tab's, shown
  only) and the outfit names over the Load page's and the Situations page's outfit details draw the outfit's icon
  tinted with its colour; a key whose PNG is not loaded draws the default key's PNG in that
  colour. The picker offers the keys of `OutfitIcons::kKeys` whose PNG is loaded, in that
  order: SkyUI's slots (helmet, circlet, body, hands, feet, necklace, ring, shield; each
  heavy, light, clothing), Jelidity's cloak and backpack, SkyUI's other items (weapons, the
  schools of magic in the game's order and magic effect, the elements, book, potion, misc),
  then BOOBIES' `bodyparts` from head to feet; ten to a row. An icon drawn the same as one listed is left out (SkyUI's forearms and calves,
  BOOBIES' torso, ring and circlet). The colours are `OutfitIcons::kColors`, each a key, a name
  and a tint: three rows of ten, the colours, then each `- Light` and `- Muted`
  (the leftmost of those rows are Nordic silver and Ash grey); each row
  Bone white, Snowberry red, Sheogorath pink, Deathbell purple, Jazbay blue, Coldharbour
  cyan, Glass teal, Spriggan green, Luna yellow and Dwarven gold. A key is never changed once
  shipped; a tint may be.
