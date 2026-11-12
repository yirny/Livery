# Livery - Outfit Manager

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that manages what you and your NPCs wear. Save what a character wears as an outfit, dress anyone in a saved outfit, and let Livery switch outfits to suit the situation. No ESP and no Papyrus scripts; see [Uninstall](#uninstall) before you remove it.

Building from source: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

---

<!-- nexus:start -->

## Requirements

- Skyrim SE or AE. Tested on 1.6.1170. Built for SE 1.5.97 and AE 1.6.x and 1.7.x; reports from other versions welcome, with your Livery.log.
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for SE and AE, matching your game version (1.7.x needs a recent release)
- Optional: SKSE Menu Framework, for changing the settings in game

## Installation

1. Install the requirements above.
2. Install Livery via your mod manager.
3. Launch Skyrim via SKSE.

## Compatibility

- No ESP/ESL and no scripts, so it sits beside any other mod without setting your PC on fire; its co-save record is simply skipped once Livery is gone. A second outfit manager on the same character gets confusing, and a second mod switching outfits by situation more so. TL;DR: unless you have a reason to, don't run it beside another outfit management mod.
- **Nether's Follower Framework**: fine, as long as you don't use its outfit features on the followers Livery dresses.
- **Tailor**: fine for wigs; leave the outfits to one of the two.
- Skyrim VR is not supported.

<!-- nexus:end -->

---

## Quick Start

### Opening Livery

Press the hotkey (Shift + V by default). Aim at an NPC to open Livery on them; aim at anything else, a horse included, to open it on yourself. Children are never available.

By default you can dress your followers and spouse, and save what your friends wear without changing it. Other NPCs are off limits. While the window is open, the HUD is hidden and the game cannot be saved.

### Dress a follower or your spouse

1. Open Livery on the NPC.
2. Select the items you want them to wear. Selecting an item deselects those on the same body slot.
3. **Apply**, then **Exit**.

**Add from Wardrobe...** puts a saved outfit's items on the list. Items selected under your own inventory move to the NPC at Apply, enchantments and tempering included.

Items Livery put on stay on: the NPC no longer swaps them for better armor you give them. Give them an enchanted or more tempered copy of an item they wear, and they put it on as the trade menu closes. **Restore Outfit** gives them back their own clothes.

### Save an outfit

1. Open Livery on yourself or the NPC, wearing what you want to save.
2. Press **Save to Wardrobe...**
3. Name it, pick an icon, **Create**, then **Exit**.

To change an existing outfit, use the **Update Existing Outfit** tab. On a friend, Livery saves what they wear but can't change it.

### Wear an outfit from the Wardrobe

1. Pick the **Wardrobe** tab on the left edge of the window.
2. Select an outfit. Deselect an item to leave it off for this character only.
3. **Apply**, then **Exit**.

Rename an outfit, change its icon or items, or remove it on the same page. Livery asks before every update and every removal.

### Set situational outfits

1. Pick the **Situations** tab on the left edge of the window.
2. Drag outfits from the list on the right onto the situations on the left. They are saved as you go.

Drag an outfit back to the list to clear it. Situational outfits are saved per character. The situations are:

- **Town**: a city, town, settlement, inn or other inhabited interior.
- **Castle**: a jarl's palace or keep.
- **Home**: a house.
- **Sleep**: going to bed, in it or getting up.
- **Cold**: outdoors in rain or snow, or anywhere outdoors in a snowy region.

The **Default** outfit is worn where no situation applies, and in every fight. A castle or home without an outfit of its own uses the Town one. With **Random Outfit** on, a situation picks one of its outfits each time it begins; click the die beside the situation worn now to switch to another of its outfits at once. You can rename or remove a situation on the same page; **Restore Presets** brings them back.

### Use item chests

Turn on **Enable chests** in the settings first.

1. Pick the **Chests** tab, press **Create New Chest...** and name it.
2. Click items from your inventory or the NPC's to put them in. **Add from Wardrobe...** puts in a whole outfit.
3. When dressing, press **Add from Chest...**, select the chest, **Load**. Select the items you want, then **Apply**.

A chest can hold several items for one body slot, such as the variants of one armor. It is never worn or given to a situation.

### Live Preview

With **Live Preview** ticked, the game keeps running and the character wears the selected items. With it off, the game is paused. Either way, you can move the camera around the character. Closing the window always ends the preview and puts back what they wore.

With the cursor over the game rather than the window:

| Input         | Camera                                                                  |
| ------------- | ----------------------------------------------------------------------- |
| W / A / S / D | Moves; hold Shift for half speed. Ignored while a text field is active. |
| Left drag     | Sideways turns the view around the character; up and down tilts it.     |
| Right drag    | Pans.                                                                   |
| Mouse wheel   | Zooms.                                                                  |

## Settings

Settings live in `Data\SKSE\Plugins\Livery.ini`, created with defaults on first start and read when the game starts. With SKSE Menu Framework installed, the same settings are on its **Livery** page and take effect at once. The window's **Appearance** tab has the skin, palette and menu sounds, with or without SKSE Menu Framework.

| Section      | Key                    | Default          | Meaning                                                                                                                                                                                                                          |
| ------------ | ---------------------- | ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `General`    | `FollowersAndSpouse`   | `Full`           | What the hotkey lets you do with followers and your spouse: `Full`, `SaveOnly` or `Off`.                                                                                                                                         |
| `General`    | `Friends`              | `SaveOnly`       | The same, for NPCs who count you as a friend or better.                                                                                                                                                                          |
| `General`    | `OtherNPCs`            | `Off`            | The same, for every other adult NPC.                                                                                                                                                                                             |
| `General`    | `LivePreview`          | `1`              | `1` opens the window with Live Preview on; `0` opens it with Live Preview off and the game paused.                                                                                                                               |
| `General`    | `NpcReaction`          | `1`              | `1` has the NPC turn to you and say a greeting when the window opens with Live Preview on.                                                                                                                                       |
| `General`    | `UnusedSlots`          | `ExceptHeadgear` | What NPCs in a Livery outfit may equip in slots the outfit doesn't use: `All`, `ExceptHeadgear` (no helmets, hoods or circlets) or `OutfitOnly`; in game **Gear in unused slots**. Their original outfit never goes back on. |
| `UI`         | `Palette`              | By UI mod        | Look of the window: a file in `Livery\palettes\`, named without `.ini` (see [Palettes](#palettes)).                                                                                                                              |
| `Hotkey`     | `Key`                  | `47`             | The hotkey as a DirectInput scan code (`47` = V). In game, click the hotkey button and press a key.                                                                                                                              |
| `Hotkey`     | `Modifier`             | `42`             | Key to hold together with `Key`: `0` none, `42` Shift, `29` Ctrl, `56` Alt (left or right).                                                                                                                                      |
| `Camera`     | `SpeedPercent`         | `100`            | Speed of the preview camera's movement, drags and wheel zoom, in percent (`10` to `200`).                                                                                                                                        |
| `Player`     | `AddMissingPieces`     | `0`              | `1` lets an outfit you give yourself add the items you do not carry, which creates items; in game the tick **Dress yourself from inventory only** is its opposite.                                                               |
| `Window`     | `ShowPlayerInventory`  | `1`              | `1` lists your armor in an NPC's window, under `<your name>'s Inventory`; never quest items.                                                                                                                                     |
| `Window`     | `ShowPlayerWornPieces` | `0`              | `1` lists the armor you wear there too; `0` leaves it out unless you carry another copy; in game the tick **Exclude equipped items** is its opposite.                                                                            |
| `Window`     | `ShowSlotColumn`       | `0`              | `1` adds a Slot column to the window's item list: each item's body slots by number.                                                                                                                                              |
| `Window`     | `ItemChests`           | `0`              | `1` adds the Chests tab and **Add from Chest...** to the window (see [Use item chests](#use-item-chests)); in game the tick **Enable chests**.                                                                                   |
| `Window`     | `OutfitSort`           | `Name`           | How the outfit lists are sorted: `Icon` or `Name`. Livery saves the column label you last clicked.                                                                                                                               |
| `Window`     | `OutfitSortDescending` | `0`              | `1` reverses that order.                                                                                                                                                                                                         |
| `Window`     | `ItemSort`             | `Name`           | How the item lists are sorted: `Name`, `Type` or `Slot`, saved the same way. Without `ShowSlotColumn`, and in an outfit's details, `Slot` sorts by name.                                                                         |
| `Window`     | `ItemSortDescending`   | `0`              | `1` reverses that order.                                                                                                                                                                                                         |
| `Transfer`   | `MoveFromPlayer`       | `1`              | `1`: Apply moves the selected items of that list to the NPC; `0` gives the NPC new copies.                                                                                                                                       |
| `Transfer`   | `ProtectFavorites`     | `1`              | `1` leaves your favorites out of that list.                                                                                                                                                                                      |
| `Situations` | `NotifyChanges`        | `0`              | `1` shows a HUD message when a character changes outfit because a situation began or ended.                                                                                                                                      |
| `Uninstall`  | `ReleaseEveryone`      | `0`              | `1` releases everyone at every save load, to uninstall without SKSE Menu Framework (see [Uninstall](#uninstall)); not on the settings page.                                                                                      |

### Palettes

Each `.ini` in `Data\SKSE\Plugins\Livery\palettes\` sets the window's skin (`Modern`, `Vanilla` or `Nordic`) and its colours. Livery ships **Modern**, **Modern - Edge**, **Vanilla** and **Nordic**, and writes each only when it is missing, so an update never undoes your edits. The first time the game starts with Livery, it picks the one that suits your UI mod: **Nordic** for NORDIC UI and Dragonborn UI, **Modern - Edge** for Edge UI and Vel'dun UI, **Modern** for Untarnished UI, Norden UI, Oathvein UI and Dear Diary DM, and **Vanilla** otherwise. To make your own, copy one under a new name in that folder and change its colours, each `#RRGGBB` or `#RRGGBBAA`; it is listed the next time the game starts.

## Files

All in `Data\SKSE\Plugins\Livery\`, shared by every save and character. Back them up with your saves.

- `outfits.json`: the Wardrobe. Items are stored by plugin name and FormID, so outfits survive a load order change.
- `situations.json`: situation names and removed situations; written only once a situation differs from the installed ones.
- `chests.json`: your item chests.

A file that cannot be read is never overwritten; `Livery.log` says why. The log is in `Documents\My Games\Skyrim Special Edition\SKSE\` (`Skyrim Special Edition GOG` for the GOG version).

## Uninstall

Press **Release everyone** on the Livery page of SKSE Menu Framework first, with a save loaded and the window closed. Without SKSE Menu Framework, set `ReleaseEveryone=1` under `[Uninstall]` in `Livery.ini` with the game closed and load your save. Either way, every NPC Livery dressed is unlocked and the items it gave them are taken back. Then save and remove Livery; it adds no plugin file, so your save loads without it. Skip the release and the NPCs keep their outfit locked on, with Livery's items lootable.

Livery never sets `ReleaseEveryone` back to `0`: set it yourself to keep playing with Livery. NPCs out of reach (not persistent, in a cell that is not loaded) are not released.

## Limits

- Only ASCII can be typed into the name field.
- Items from plugins that are not loaded have no row in the window and stay in the outfit. Edit `outfits.json` to remove them.
- Gear another mod puts on an NPC can stay on in a slot their outfit leaves empty, even when **Gear in unused slots** keeps it out. It comes off the next time you trade with them or they load in.
- Your Sleep outfit goes on only with a mod that puts you into bed: the game's own beds just pass the time.
- The copies Livery adds to an NPC who has no default outfit show in the trade menu, where you can take them for free.

## License

Copyright (c) 2026 Livery Contributors. Licensed under the GNU GPL v3.0 or later; see [LICENSE](LICENSE). CommonLibSSE-NG is also GPL-3.0-or-later. The SKSE Menu Framework header in `lib/extern/` is LGPL-2.1; the Jost, Oswald and Sovngarde fonts are under the SIL Open Font License; the Nordic skin's ornaments are NORDIC UI art by Dopesan, used under the permissions on its mod page; the list heading rule is SkyUI art by the SkyUI Team, used under the permissions on its mod page (see `Data/SKSE/Plugins/Livery/textures/SKYUI-CREDITS.txt`); the item icons are SkyUI art by PsychoSteve and Jelidity, used with their permission (see `Data/SKSE/Plugins/Livery/icons/ICON-CREDITS.txt`).
