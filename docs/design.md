# Livery: design

Written 2026-10-19 for the AI and the developer who implement it. Current spec only;
the decisions below were settled with the user and verified where marked. The product
is Livery - Immersive Outfit Management (`livery.dll`, `Livery.ini`, `Livery.log`,
`Data/SKSE/Plugins/Livery/`). This repository evolves into it: the Tailor add-on it was
keeps its ImGui window, input hook, theme, preview camera and inventory code; the
Tailor API dependency goes away in stage 2. The dressing core is `src/Dressing.cpp`,
the assignments and co-save `src/Assignments.cpp`.

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
   co-save (§5). Copies are made only here, at assignment time; nothing else ever
   creates one.
3. Equip each chosen instance with `ActorEquipManager::EquipObject(..., a_forceEquip =
   true)`. `forceEquip` is the game's PreventRemoval flag: the engine puts
   `ExtraCannotWear` on the worn instance and its best-gear pass (`UpdateWornGear`)
   skips that slot, so better armor the player gives later stays in the inventory.
   A piece already worn gets the flag added directly (`ExtraDataList::Add(new
   RE::ExtraCannotWear())` + `AddChange(kInventory)`), Tailor's way for wigs.
4. Take off worn armor that is not part of the outfit, except non-playable armor and
   pieces another mod locked (a lock already on a worn piece is never claimed). Only
   when at least one piece went on: otherwise nothing is assigned and the actor keeps
   what they wear. An earlier assignment of the same actor is released first.

Slots the outfit leaves empty stay the engine's: at its next re-evaluation an NPC can
put on armor they own there (a helmet, a shield). Accepted for now.

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
- Unassign: release the mod's locks, remove the mod's copies still present (by co-save
  key; never a player-given instance), forget the actor. The NPC returns to vanilla
  behaviour at the engine's next re-evaluation, which can be a cell change or two
  later; this delay is accepted.
- Death: release the mod's locks (`TESDeathEvent`) and forget the actor, so the corpse
  behaves like vanilla; the copies stay on it, lootable.
- "Release everything" command for uninstall: unassign every managed actor. Actors not
  in memory (non-persistent NPCs whose cell is not loaded) cannot be reached and keep
  their locks and copies. Locks and copies are otherwise the only residue after removal;
  they do not break a save.

### The player

Same dressing path, three differences: no lock (a locked piece cannot be taken off in
the inventory menu, verified); adding pieces the player lacks is OFF by default,
switchable in the settings (`[Player] AddMissingPieces`) with a warning that it creates
items; nothing is put back on the player when they load.

### Contracts

Building calls and anything taking `RE::Actor*` run on the game thread (SKSE task).
Raw inventory pointers are read again after every native call; an instance is found
again only by its `ExtraUniqueID` key.

## 3. Hiding the mod's copies in the trade menu (stage 3)

Copies the mod adds are ordinary items, so the trade menu shows them and the player
could take them for free. Hiding uses the inventory-filter hook SPID uses for its
distributed outfits (`SPID/src/Outfits/OutfitManager+Hooks.cpp`, runtime 1.6.1130+:
`REL::ID(51145)+0xEB`, `REL::ID(51146)+0x15B`, `REL::ID(51144)+0x10F`; verify against
the current Address Library before writing). Inside the call, remove the entries whose
instance matches a co-save key. Only the mod's copies are hidden: player-given instances
stay visible and takeable even while they are part of the outfit (taking back one's own
item is not a dupe). Corpses are not filtered, as with vanilla outfit items.

Rejected: tagging copies with `ExtraOutfitItem` (id = the NPC's DOFT hides them in vanilla,
but any `SetOutfit` then deletes them, and a non-outfit id has no precedent).

## 4. Data

- Outfit library: own JSON under `Data/SKSE/Plugins/Livery/`, shared by every save.
  Armor identity = local FormID + plugin file name, resolved with
  `TESDataHandler::LookupForm` (ESL ids are 12-bit), as Tailor's `outfits.json`.
- Settings: INI, as today.

## 5. Co-save

Co-save id `LVRY`, one record `ASGN` (version 1), read in the SKSE load callback,
cleared on revert:

- Per assigned actor, the player included (FormID, fixed with `ResolveFormID`): the
  pieces Livery put on them, each as its instance key `(armor FormID, ExtraUniqueID
  baseID, uniqueID)` and two flags: Livery added the copy, Livery set its lock. NPCs
  are locked, the player never, so no per-actor lock flag is stored.
- Stage 2 adds the assigned outfit's id. Until then, and for the pieces themselves, an
  edit of the library reaches an actor only when the player assigns again.
- A piece whose plugin is gone is dropped at load, and an assignment left without
  pieces. A key whose instance is gone is skipped whenever the actor is dressed or
  released.

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
- Sources for the PreventRemoval semantics: Tailor `src/wig/WigEquipment.cpp`
  (prevents removal when worn, equip when unworn), SPID `SPID/src/Outfits/Helpers.h`
  (blocks the slot in `UpdateWornGear`).

## 7. Plan

Each stage ends with an in-game check before the next starts.

1. Rename the project (CMake, DLL, INI, log, deploy folder; the directory name stays
   until the memory is moved), settings, co-save, dressing core: assign, unassign,
   load-time re-equip, death release, release everything. Hotkeys only: Shift+F10
   copies what the target wears, F10 assigns the copy, Alt+F10 unassigns, F8 releases
   everyone (checked before the capture hotkey, whose default is also F8); the target
   is the crosshair NPC, else the player.
2. Library: capture the worn pieces into an outfit, JSON storage, the ImGui window,
   outfit list and preview from today's code. Remove the Tailor API and rewrite
   CLAUDE.md §2.1 for the mod's own data.
3. The trade-menu filter hook (§3).
4. Later, as needed: situation switching (town, home, sleep...), assignment export/import,
   an export to Tailor's import JSON.
