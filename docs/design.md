# Livery: design

Written 2026-10-19 for the AI and the developer who implement it. Current spec only;
the decisions below were settled with the user and verified where marked. The product
is Livery - Immersive Outfit Management (`livery.dll`, `Livery.ini`, `Livery.log`,
`Data/SKSE/Plugins/Livery/`). This repository evolved from a Tailor add-on and keeps
its ImGui window, input hook, theme, preview camera and inventory code; the Tailor API
dependency went away in stage 2. The dressing core is `src/Dressing.cpp`, the
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
   worn copy, enchanted copy, highest-tempered copy, plain copy. Player enchantments
   (`ExtraEnchantment`) and tempering (`ExtraHealth`) live on the instance, so the
   player's own gear keeps them. Tailor's player wardrobe uses the same order. An
   unworn instance that carries `ExtraCannotWear` cannot be equipped and is skipped.
2. When the actor has none, add a plain copy (`AddObjectToContainer`), give it an
   `ExtraUniqueID` (`InventoryChanges::GetNextUniqueID`) and record the key in the
   co-save (§5). Copies that stay are made only here, at assignment time; the live
   preview's own copies go back when it ends.
3. Equip each chosen instance with `ActorEquipManager::EquipObject(..., a_forceEquip =
   true)`. `forceEquip` is the game's PreventRemoval flag: the engine puts
   `ExtraCannotWear` on the worn instance and its best-gear pass (`UpdateWornGear`)
   skips that slot, so better armor the player gives later stays in the inventory.
   A piece already worn gets the flag added directly (`ExtraDataList::Add(new
   RE::ExtraCannotWear())` + `AddChange(kInventory)`), Tailor's way for wigs.
4. Take off worn armor that is not part of the outfit, except non-playable armor and
   pieces another mod locked (a lock already on a worn piece is never claimed). Only
   when at least one piece went on: otherwise nothing is assigned and the actor keeps
   what they wear. Every copy worn before step 1 is tagged (`ExtraUniqueID`) first; the
   ones that are off at the end, taken off here or by the engine for a slot a piece
   filled, are recorded as the actor's own pieces (§5) and go back on at release. An
   earlier assignment of the same actor is released first, its own pieces back on, so
   they are what the new one records.

Slots the outfit leaves empty stay the engine's: at its next re-evaluation an NPC can
put on armor they own there (a helmet, a shield). Accepted for now.

Pieces from the player's inventory (the window's `From Player's Inventory`) move to an NPC
at Apply, before the dressing, when `[Transfer] MoveFromPlayer` is on
(`Dressing::MoveFromPlayer`): `TESObjectREFR::RemoveItem(armor, 1, kStoreInContainer,
extraList, npc)`, the container menu's path, so the instance keeps its extra data. The copy
given is an unworn one ranked as step 1 ranks them, the worn one only with
`ShowPlayerWornPieces` on and no other. The NPC's inventory is read again after the moves;
a piece that did not arrive is left out of that dressing and counted on the HUD, never
replaced by a copy. A moved instance is the NPC's own: step 1 picks it, it is not recorded
as added, and Unassign leaves it with them. Quest items (`InventoryEntryData::IsQuestObject`)
are never offered, favorites (`IsFavorited`) not while `ProtectFavorites` is on. The
preview and the Save page move nothing.

The outfit the window's Apply gives (`Assignments::Assign`) is the actor's default. An outfit chosen for a situation (§7)
replaces it while the situation lasts, by the same steps: the worn pieces are released
(copies removed, the actor's own pieces back on) and the new outfit is dressed. When the
actor wears a library outfit and the ticks differ from it, Apply asks first: Yes updates
that outfit's pieces (`Library::Update`, name, gender and classes kept), then the Default
gets the outfit, or, for a situation outfit, `Assignments::Refresh` dresses the updated
outfit with the Default untouched; No, apply as is makes the ticks the Default's pieces,
not a saved outfit, with `chosen` 0 and random off. The library write comes first; a
failed one moves and assigns nothing.

Never through `TESNPC::defaultOutfit` (DOFT): `Actor::SetDefaultOutfit` writes an NPC_
change form into the save, a runtime 0xFF outfit does not survive a load,
`RemoveOutfitItems` strips the NPC's own copies, and it is per base record, not per
reference. The DOFT path also does not stop the better-armor swap (SPID's description
says so), so a lock would be needed anyway.

### Keeping the outfit on

- `ExtraCannotWear` is saved with the instance, so nothing is re-applied on load.
- On `TESObjectLoadedEvent` for a managed NPC: if the instance chosen for a piece is
  still in the inventory but not worn (another mod took it off; taking off clears the
  lock), equip it again with the lock. Pieces whose instance is gone are left alone. No
  copies are created here. The sink only checks a mutex-guarded id list and queues an
  SKSE task, since the event's thread is not documented.
- A locked piece the player takes back in the trade menu stays taken: the slot is left
  to the vanilla picker until the player assigns again.
- Unassign (the window's Restore Outfit): release the mod's locks, remove the mod's copies
  still present (by co-save key; never a player-given instance), put the actor's own
  pieces it took off back on (those still carried, unworn and unlocked, without a lock),
  forget the actor. A slot whose own piece is gone is left to the engine's next
  re-evaluation, which can be a cell change or two later.
- Death: release the mod's locks (`TESDeathEvent`) and forget the actor, so the corpse
  behaves like vanilla; the copies stay on it, lootable, and its own pieces are not put
  back on.
- Release everything (F8, for uninstall): unassign every managed actor. Actors not
  in memory (non-persistent NPCs whose cell is not loaded) cannot be reached and keep
  their locks and copies. Locks and copies are otherwise the only residue after removal;
  they do not break a save.

### The player

Same dressing path, three differences: no lock (a locked piece cannot be taken off in
the inventory menu, verified); adding pieces the player lacks is OFF by default,
switchable in the settings (`[Player] AddMissingPieces`) with a warning that it creates
items; nothing is put back on the player when they load. Situation outfits (§7) switch
on the player as on an NPC, unlocked like the default, and not in beast form.

### Contracts

Building calls and anything taking `RE::Actor*` run on the game thread (SKSE task).
Raw inventory pointers are read again after every native call; an instance is found
again only by its `ExtraUniqueID` key. `InitInventoryIfRequired` is never called before
worn state is read: on an NPC who wears outfit items it takes `ExtraWorn` off every
outfit copy (§6).

## 3. Hiding the mod's copies in the trade menu (stage 3)

Copies the mod adds are ordinary items, so the trade, container and gift menus would list
them and the player could take them for free. The engine hides an NPC's own outfit items in
those menus by one rule, applied inside the functions that fill a menu's item list from an
inventory: an entry whose `ExtraOutfitItem` names the actor base's current `defaultOutfit` is
skipped. `src/TradeFilter.cpp` hooks the calls to those functions, the ones SPID hooks for
its distributed outfits (`SPID/src/Outfits/OutfitManager+Hooks.cpp`, `FilterInventoryItems*`;
SPID swaps `defaultOutfit` around the call). Around the original call, the copies Livery added
to an assigned NPC (the co-save's `added` keys) carry a temporary `ExtraOutfitItem` with the
NPC's `defaultOutfit` (`Dressing::MarkAsOutfitItems`); it is taken off again before the call
returns, so no `SetOutfit` can ever see it and nothing is flagged as changed. Only the mod's
copies are hidden: player-given instances stay visible and takeable even while they are part
of the outfit (taking back one's own item is not a dupe). Not filtered: the player's own list
(a copy added to the player is the player's item), an NPC without a default outfit, and
corpses (Livery forgets an actor at death, and vanilla shows a corpse's outfit items too).

Call sites, Address Library IDs (they exist in the 1.6.1170 and 1.6.1179 databases): AE
1.6.1130+ `51145+0xEB`, `51146+0x15B`, `51144+0x10F` (an AE function inlines the caller of
the first); SE 1.5.97 `50216+0xED`, `50217+0x152`. The Steam executable is DRM-wrapped, so the
bytes are checked at install instead: a site must match SPID's signature, the `call` and the
instructions after it (`E8 ?? ?? ?? ?? 90 48 85 DB` for the list fill), or it is left alone
with a warning in the log. Skyrim VR and AE before 1.6.1130 are not covered.

Rejected: a permanent `ExtraOutfitItem` on the copies (id = the NPC's DOFT hides them in
vanilla, but any `SetOutfit` then deletes them, and a non-outfit id has no precedent);
removing the entries from the filled `ItemList` (its GFx array and its engine-heap `Item`s
would have to be kept in step).

## 4. Data

- Outfit library: own JSON under `Data/SKSE/Plugins/Livery/`, shared by every save.
  Armor identity = local FormID + plugin file name, resolved with
  `TESDataHandler::LookupForm` (ESL ids are 12-bit), as Tailor's `outfits.json`.
- Categories (the window's Category; the window calls the library the wardrobe): Light,
  Heavy and Clothing, SkyUI's armor classes in `ArmorInfo.h` but Jewelry, which
  `Library::Load` drops in memory. Other stored categories (older files' situation
  categories: Adventuring, Town, ...) stay in the file and are not shown (§8 item 8).
  Situations are per actor (§7), not in the library.
- `Library::Update` replaces pieces, the categories the window offers, name and gender, and
  keeps the other stored categories; a name is taken when another outfit has it (ASCII case
  ignored).
- `Library::Remove` takes an outfit out; ids are never reused. `Assignments::ForgetOutfit`
  then takes it off every row in this save (a `chosen` that was it becomes 0), and a Default
  this leaves wearing nothing gets the outfit's pieces, read before the remove, as its
  pieces. Other saves' rows naming it keep the id, which counts as not on the row and never
  shows (docs/situation-management-ui.md §1); restoring outfits.json brings it back.
- Situation data: `situations.json` beside the library (`src/SituationStore.cpp`; the
  presets, the file's parse and write and the name rules in `src/SituationTable.h`): per
  situation key its name, description and removed flag, only for the situations that
  differ from the presets; a missing or empty file is every preset as installed and is not
  written at load. The same load and write rules as the library (docs/situation-management-ui.md
  §2); an empty `outfits.json` is an empty library too. Loaded at `kDataLoaded`, before the library and any co-save load.
- Settings: INI, as today.

## 5. Co-save

Co-save id `LVRY`, one record `ASGN` (version 4; versions 1 to 3 are still read), read
in the SKSE load callback, cleared on revert:

- Per assigned actor, the player included (FormID, fixed with `ResolveFormID`): the
  library outfit the pieces on them are from (`wornOutfit`, 0 for none or the Default's
  pieces), the pieces Livery has on them, each as its instance key `(armor FormID,
  ExtraUniqueID baseID, uniqueID)` and two flags: Livery added the copy, Livery set its
  lock. NPCs are locked, the player never, so no per-actor lock flag is stored. Then the
  instance keys of their own copies Livery took off (§2 step 4).
- The Default's pieces: armor FormIDs not in the wardrobe, worn while the Default's row
  wears no outfit (docs/situation-management-ui.md §1). Then which row the pieces are for
  (one byte, the `Situations::Situation` value), and the rows (`OutfitRows`): a count, and
  per row its situation (one byte), random, `chosen` and its library outfit ids. Every row
  reads its outfit from the library each time it goes on; the window names the outfits
  from the ids. A row for a situation the version does not know is dropped with a log line.
- Version 3 has the default's outfit id and armor and one library outfit id per situation
  (`Situations::kChoosable` order) instead of the rows. Version 1 has no outfit id and no
  own pieces; versions 1 and 2 have no default armor and no situations: their pieces'
  armor becomes the default. At load, a default whose outfit id the library has becomes
  the Default's row (`outfits = [id]`, `chosen = id`) and its armor is dropped; else its
  armor becomes the Default's pieces. Each situation id becomes a row of it, chosen; random
  is off everywhere. The library is loaded at `kDataLoaded`, before any co-save load.
- A piece, own copy or Default piece whose plugin is gone is dropped at load, and an
  assignment with nothing on the actor and no situation row holding an outfit. Before that
  check, the row of a situation removed in `situations.json` (§4) is dropped, with a log
  line; pieces worn for it stay on until the poll's next dressing. A key whose
  instance is gone is skipped whenever the actor is dressed or released.

Per character and per save by construction. An export/import of assignment sets (JSON)
can come later if sharing across characters is wanted.

## 6. Verified facts (2026-10-19, in game, runtime 1.6.1170)

- `ExtraCannotWear` on an NPC's worn pieces: better armor given in trade is not put on;
  survives save/load and fast travel.
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
- An NPC's own outfit copy taken off and equipped again, the same instance in a later
  frame, goes back on (live preview).
- Situation outfits on a follower (§7): Town in a town and the default outside it; Warm
  outdoors in cloudy, rainy or snowy weather (a region that is not snowy); the default
  during a fight and the situation outfit after it; the choices and the switching survive
  save and load; a version 2 co-save record is read.
- Clearing situation choices (§7): two cleared at once; Town cleared while in a town put
  the default back on at once; the cleared choices stayed cleared across save and load.
- Situation outfits on the player (§7): Town in Whiterun, Warm by weather, and Swimming
  when wading on foot; no redress while a werewolf, and the right outfit on after turning
  back; wading on horseback is not Swimming; the choices survive save and load.
- Sources for the PreventRemoval semantics: Tailor `src/wig/WigEquipment.cpp`
  (prevents removal when worn, equip when unworn), SPID `SPID/src/Outfits/Helpers.h`
  (blocks the slot in `UpdateWornGear`).

## 7. Situations

An assigned actor has a row per situation, Town, Home, Sleep, Swimming, Warm
(`Situations::kChoosable`), and the Default, what the window's Apply gives, worn where
none of them applies. A row is a list of library outfit ids, a random flag and the one
chosen (docs/situation-management-ui.md §1, `src/OutfitRows.h`); the Default also keeps
pieces not in the wardrobe. Every row reads its outfit from the library each time it goes
on. Each situation's name and description come from `situations.json` (§4): the window and
the HUD show the name, the sentences that say what is worn end in `, <description>`
(nothing for an empty one), and the logs keep the key. A removed situation offers no row
and nothing wears it. Random off, the row wears `chosen`; random on, one of the list is picked as the row
goes on and kept until it changes, a forced redress keeping it while it is on the list,
and recorded as `chosen`, so turning random off keeps the last pick on. Giving a row an
outfit (`OutfitRows::Give`) with random on puts it at the front of the list; with random
off it becomes `chosen` in the place of the outfit it replaces, which leaves the list, so
the list keeps its length. The Save page's `Situations for
#{Name}` rows give and take the outfit for the captured actor in one `Assignments::Change`, after
the library write, with one forced redress. Manage Wardrobe's `Situations for #{Name}` edits the selected outfit's rows (Default
included) with it; Update makes them at once (`Assignments::ChangeRows`) and dresses them
once as Manage Situations does, and its `Apply to #{Name}` dresses the
actor for the ticked rows and takes it off the unticked ones; with none ticked it also
makes the outfit the default, unless the user unticked the Default for it.
Manage Situations (docs/situation-management-ui.md §3-§6) edits every row of the captured
actor by drag and drop, the random flags included, and the situations' names and
descriptions; its Save Changes writes `situations.json` first, then replaces the rows
(`Assignments::SetRows`, the record only) and dresses them once (`Assignments::Refresh`,
at once with Live Preview on, else at the window's close; a random row worn now wears the
outfit it gained first). Removing a situation writes `situations.json`, then clears the
situation's rows in this save (`Assignments::ForgetSituation`) and takes those wearing it
out of it as Remove takes them out of a removed outfit; Restore Presets changes no row. An actor can have situation outfits without a default and then wears their
own clothes outside them. Restore Outfit forgets the choices with the rest. The
player has the same rows.

### Detection (`Situations::Evaluate`)

The situations an actor is in, the most specific first (`Situations::Applicable`); the
first one whose row holds an outfit is worn, else the Default (`OutfitRows::Resolve`). The rules are Tailor 3.0's
(`src/events/SituationHandler.cpp` and its policy headers). Verified in game in Livery
(§6): Town, Warm by weather, combat, Swimming by wading. Not yet: Home, Sleep, Warm by a
snowy region. The rules:

- In combat: none, so a fight is fought in the default outfit.
- Swimming: the actor state's swimming flag, or the engine reports a water height
  relevant to them no lower than 40 units under their feet (wading counts). On a mount
  only the swimming flag counts: a rider's feet are above the water the horse wades.
- Sleep: sit/sleep state `kWantToSleep` to `kWantToWake` (the other states are chairs
  and mounts), or on furniture flagged CanSleep. Vanilla beds never put the player into
  bed (Tailor's note in `SituationHandler.cpp`), so the player's Sleep needs a mod that does.
- Warm: in an exterior cell and the weather is cloudy, rainy or snowy (while the sky
  blends weathers, the last one decides until the new one is halfway in), or the sky's
  weather region, the one around the player, has snow weathers for at least a quarter
  of its weather chance (its clear days are its own).
- Home: the current location has `LocTypePlayerHouse`, or `LocTypeHouse` for an NPC.
  Town follows it, so a home without a Home outfit wears the Town one.
- Town: `LocTypeCity`, `LocTypeTown`, `LocTypeSettlement`, `LocTypeDwelling` or
  `LocTypeInn`. Keywords keep their editor IDs at runtime and are found by them.

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
weather turns for everyone, the others follow at the next polls. A change is acted on
once: a situation outfit that puts nothing on falls back to the default and is not tried
again until the situations change. With `[Situations] NotifyChanges` on (off by default),
each such redress shows a HUD message naming what they wear now. Apply and the Save page
dress for the current situations at once. The poll stops at `kPreLoadGame` and starts at `kPostLoadGame` and
`kNewGame`, forgetting what it last saw. The record's `pieces` describe whatever is worn,
so a situation outfit is kept on, locked and put back at 3D load like the default.

## 8. Plan

Each stage ends with an in-game check before the next starts.

1. Rename the project (CMake, DLL, INI, log, deploy folder, repository directory),
   settings, co-save, dressing core: assign, unassign,
   load-time re-equip, death release, release everything (F8, checked before the
   capture hotkey). Done.
2. Library: capture the worn pieces into an outfit, JSON storage, the ImGui window,
   outfit list and preview from the add-on's code; the preview dresses everyone through
   the inventory; the window's Assign and Release replace the stage-1 keys, and the
   capture hotkey defaults to F10. Remove the Tailor API and rewrite CLAUDE.md §2.1 for
   the mod's own data. Done.
3. The trade-menu filter hook (§3). Done.
4. Situation outfits for NPCs (§7): detection, the poll, the Assign for dropdown, the
   Situations page with its per-situation clear. Done.
5. The player's situations (§7): the window's dropdown and Situations page, the poll
   skipping beast form, the mount rule for water. Done.
6. Rework the capture window's existing pages (docs/ui-redesign.md). Step 1, the Capture
   page: done. Step 2, the Save page: done. Step 3, Apply (its popup and the transfer from
   the player): done; that quest items stay out of the player's list is not yet checked in
   game.
7. Outfit management in the capture window (docs/outfit-management-ui.md): step 1, the
   outfit list and the Inventory page's buttons: done. Step 2, the tab column and the
   Manage Wardrobe page with Update, the captured actor redressed while the window is open
   (§6 there): done. Step 3, Remove, which deals with the assignments and situation
   choices that name the outfit: done.
8. Adding categories beyond Light, Heavy and Clothing (a separate task), which the outfit
   list would show and sort by as it does those three; the stored categories the window does
   not offer stay unshown until then.
9. Situation management (docs/situation-management-ui.md): step A, the rows with their
   random picks, and step B, `situations.json`: done. Step C, the Manage Situations page:
   done. Later, user-defined conditions in the way of Skyrim Outfit Equipment System NG,
   extending `Situations::Applicable` (§7).
10. The open in-game checks: Home, Sleep and Warm by a snowy region (§7); quickload with
    the window open, then New Game; the player's left-hand weapon back after a shield
    preview; tempered player gear after Cancel; a follower's given armor gone after Load
    Outfit + Cancel (seen while Tailor managed her).
