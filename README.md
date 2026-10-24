# Livery - Immersive Outfit Management

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that saves the armor an NPC, or you, are wearing as an
outfit in its own library, and dresses any character in a saved outfit, keeping an NPC in it. Aim at the NPC (or at
nothing, for yourself), press the hotkey, tick the items in a small window, done. No ESP, no Papyrus; removing it
cannot break a save ([design](docs/design.md)).

Built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), [CMake](https://cmake.org) and [vcpkg](https://vcpkg.io).

---

<!-- nexus:start -->

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for SE and AE
- Optional: SKSE Menu Framework, for changing the settings in game

## Installation

**Mod manager (recommended):**

1. Install the requirements above.
2. Install Livery via your mod manager.
3. Launch Skyrim via SKSE.

**Manual:**

1. Install the requirements above.
2. Copy the `SKSE` folder from the archive into `Data\` (it holds `livery.dll` and the fonts and textures of the window skins; Livery writes the palettes on first start).
3. Launch Skyrim via SKSE.

## Compatibility

- Skyrim SE and AE. Skyrim VR is not supported.
- No ESP/ESL. Livery will not corrupt a save or set your PC on fire next to any other mod: no scripts, and its co-save record is skipped when it is gone. A second outfit manager on the same character gets confusing, and a second mod switching outfits by situation more so. TL;DR: unless you have a reason to, don't run it beside another outfit management mod.
- **Nether's Follower Framework**: fine, as long as you don't use its outfit features on the followers Livery dresses.
- **Tailor**: fine for wigs; leave the outfits to one of the two.
- **Devious Devices**: not tested. Don't dress a character wearing devices.

<!-- nexus:end -->

---

## What the Plugin Does

1. Load a save or start a new game.
2. Aim the crosshair at a follower or your spouse (any adult NPC once `RestrictToFollowersAndSpouse` is off; never a child). With nothing under the crosshair the hotkey opens Livery on you instead.
3. Press the hotkey (`[Hotkey] Key`, see [Settings](#settings); Shift + X by default). The window opens and the character wears the ticked items (see [Live Preview](#live-preview)); with Live Preview off the game freezes instead. With Live Preview on, the NPC turns their head to you and says a greeting (see `NpcReaction` in [Settings](#settings)). While the window is open the HUD is hidden, the game cannot be saved, and an NPC stays where they stand.

The window has six pages. A column of icon tabs at its left edge switches between the Inventory page, Manage Wardrobe and Manage Situations; hover a tab for its name. Switching keeps the Inventory page's ticks until the window closes. Leaving Manage Wardrobe or Manage Situations with unsaved edits, by a tab, **Exit** or Esc, first asks `You have unsaved changes.` (on Manage Wardrobe `You have unsaved changes for "Outfit".` and what changes, as **Update** lists it): **Save Changes** saves them as that page does, then goes, staying when the save is refused; **Discard Changes** drops them and goes; **Back** stays.

- **Inventory** — the heading reads `Lydia's Inventory` and, when Livery dresses the character, shows the outfit under it and the situation it is worn for: `Wearing "Jarl's Finery" for Town.`, `Wearing "Jarl's Finery" as Default.`, or `Dressed by Livery.` for items that are not a saved outfit. When some of those items are no longer on them, taken back in the trade menu or taken off, it says how many and that the outfit is only assigned: `Assigned "Jarl's Finery" as Default, 1 item not worn.`; worn armor Livery did not put on is counted too: `Wearing "Jarl's Finery" as Default, 1 extra item worn.` Apply on Manage Wardrobe then dresses them in it again, giving an NPC a copy of an item they no longer carry and taking the extra items off. The list shows every item under a heading for where it comes from, in capitals: `Outfit ("Jarl's Finery")` for the items Livery put on from that saved outfit, `Outfit (Not in Wardrobe)` for those Livery put on that are not a saved outfit, `Outfit (Unmanaged)` for the worn items Livery did not put on, `Inventory` for carried ones, her own outfit's items aside while Livery has them off her (the trade menu does not list those either, and Restore puts them back on), `<your name>'s Inventory` for your armor when the character is an NPC (see `ShowPlayerInventory` in [Settings](#settings)), and `"Outfit"` for the items of a loaded outfit that nobody carries. An item you and the NPC both carry is listed once, under the NPC's heading. Each heading starts with an icon: a backpack over carried items, a dressed form over worn ones and a bare form over a loaded outfit's. The Name and Type columns use SkyUI's names, and Type shows the name an Inventory Interface Information Injector icon pack gives an item, such as Cloak or Underwear, in the game's language; with `ShowSlotColumn` on (see [Settings](#settings)), a Slot column lists an item's body slots by number, such as `32, 34`; click a column's label to sort by it, and again to reverse the order. Left of each name is the item's icon: SkyUI's for its armor class and slot, or the one an Inventory Interface Information Injector icon pack gives it, when that icon's PNG is in `Data\SKSE\Plugins\Livery\icons\` (see `docs/design.md` §9). Worn items start ticked except wigs (items on the Hair slot only); the others start unticked. Ticking an item unticks the ticked ones that share a body slot with it. The item count is under the list, ending in **Add items from Wardrobe...**, which opens the Load Outfit page and is greyed while the wardrobe has no outfits; then the **Live Preview** tick, which turns the preview on and off. **Create / Update Outfit** and **Apply** act on the ticked items, so each needs at least one (see [Outfit assignments](#outfit-assignments) for Apply); **Exit** or Esc closes the window and changes nothing. While Livery dresses the character, **Restore** at the right end of the button row gives them back their own clothes after asking (see [Outfit assignments](#outfit-assignments)); hover its `(?)` for what it does. The `<your name>'s Inventory` heading has a `(?)` too, which says what Apply does with those items. Create / Update Outfit and Apply say what they do on hover. Throughout the window, a greyed button shows why on hover.
- **Load Outfit** — the wardrobe's outfits (left), with the selected one's items (right). **Load** ticks exactly its items and returns, adding those the character does not carry to the list; it is greyed for an outfit none of whose items is loaded. **Back** returns with the ticks unchanged.
- **Save** (after **Create / Update Outfit** on the Inventory page) — two tabs as the page's heading, **Create New Outfit** and **Update Existing Outfit**, each keeping the name you typed on it until the window closes. Both save the ticked items with an outfit name; neither gives the outfit to a situation (drag it onto one on Manage Situations, see [Situations](#situations)). **Create New Outfit** starts from the first free `New Outfit NNN` and lists the ticked items under their count; Enter or **Create** saves it. **Create and Apply** saves it the same way and dresses the character in it at once, as **Apply to Lydia** on Manage Wardrobe does, the items ticked under `<your name>'s Inventory` moved to them first as at Apply: while they wear another outfit for a situation it asks first (`Lydia is wearing their Town outfit, "B". Create "A", then make it their new Town outfit?`): **Yes** makes it that situation's outfit, **Apply as Default** makes it their Default instead, and **Cancel** goes back and saves nothing. Otherwise it makes the outfit their Default, asking first about a Default it replaces. **Update Existing Outfit** shows three columns: the wardrobe's outfits, the chosen one's items as saved (**Current Items**) and the ticked items that replace them (**New Items**). It opens with the outfit you chose there before, or else the one you last loaded, or else the one the character wears, chosen; click another outfit to choose it, or the chosen one to unchoose it. Choosing one fills the name from it and shows it on the live preview. **Update** asks for confirmation, then replaces the outfit's items with the ticked ones and gives it the name in the tab; the confirmation lists a rename and how many of its items come from plugins that are not loaded, which stay in it. It changes no situation; when the character wears the outfit, they are dressed in it as saved. **Back** returns to the items. An empty name field says so inside the field; when a save fails, the field turns red and shows the reason on hover until you edit the name, and nothing is given to the character.
- **Manage Wardrobe** (the second tab) — the wardrobe's outfits (left) and the selected one's icon, name and items (right), which you edit in place; the items' count ends in **Edit**, which opens the Edit page. Click the icon left of the name to choose another icon and its colour: the icons are SkyUI's, Jelidity's cloak and backpack, and BOOBIES' when its PNGs are in `Data\SKSE\Plugins\Livery\icons\`; hovering a colour shows every icon in it, and **OK** keeps the choice as an edit, which **Update** saves. **Update** is greyed until the outfit differs from the saved one and while the name is empty; it lists what changes (the items, a rename, the icon) and asks, then saves it, keeping its items from plugins that are not loaded; **Confirm** saves, **Cancel** goes back. It changes no situation. A name another outfit has is refused, and nothing changes. The window stays open with the list rebuilt and the outfit still selected. When the character wears that outfit, as their Default or for the situation they are in, they are dressed in it as saved at once, Live Preview on or off, and the Inventory page then lists and ticks what they wear, in place of your ticks there. **Remove**, at the right end of the button row, takes the selected outfit out of the wardrobe after a popup that names it and lists who has it in this save (`Lydia: Default, Town`, or the FormID of a character not nearby). It leaves every situation it was chosen for, which falls back to the next one that applies, or the Default, even where another outfit was chosen before it; a Default it leaves with nothing keeps its items, no longer tied to a saved outfit. A character wearing it for a situation changes at once ((the one in the window, the Inventory page ticking what they wear)), when the window closes (the others nearby), or, when not nearby, the next time they load. One HUD message sums it up, and nothing is selected afterwards. Other saves that chose it keep the choice out of sight: it no longer shows or goes on there, and putting the outfit back in `outfits.json` brings it back. **Apply to Lydia** changes what the character wears now, the window staying open as after **Apply** (see [Outfit assignments](#outfit-assignments)); hover it for what it will do. While they wear another outfit for a situation, it asks first (`Lydia is wearing their Town outfit, "B". Make "A" their new Town outfit?`): **Yes** makes it that situation's outfit in place of the one worn (with **Random Outfit** on, adds it to the situation's outfits and wears it now); **Apply as Default** makes it their Default instead, which shows once no situational outfit applies, as its `(?)` says; **Cancel** goes back. Otherwise it makes the outfit their Default, or dresses them in it again when they wear it for the situation they are in. Unsaved edits are saved first, as **Update** does, and a refused save applies nothing; it asks first about them (**Update and Apply**) and about a Default it replaces (`The current Default, "A", will be replaced.`). The edits belong to the selected outfit: selecting another one while they are unsaved asks first, listing what changes: **Discard changes** or **Keep editing**. **Exit** or Esc closes the window, first asking about unsaved edits, whose **Save Changes** goes through **Update**'s popup.
- **Manage Situations** (the third tab) — `Lydia's Situational Outfits`: the Situation List (left), with a heading per situation, Default first, and the outfit list or the selected situation (right). Each heading shows the situation's name, how many outfits are on its list while **Random Outfit** is on, a chevron that folds it, and **Edit** at its right end; click the name or **Edit** to select the situation and show its details, and the name again while they show to unselect it. Under a heading are the outfits the character can wear there: every outfit on the list while **Random Outfit** is on, else only the one worn; the Default shows `Items not in the wardrobe` while it wears items that are not a saved outfit. A situation that wears nothing has its name greyed and no chevron. Click an outfit's row, in either pane, to see its details on the right: its name, the situations the page gives it and its items, with the outfit on the preview; **Edit** opens it on Manage Wardrobe (asking first about unsaved edits), and **Back** or Esc returns. The outfits under a situation have no star, since each is chosen for one. Drag an outfit by its row, marked by dots at its left, from the outfit list onto a situation to add it at the front, between two of its outfits to place it there, from one situation onto another to move it, or back onto the outfit list to take it off (the Default's items too); a line shows where it goes. With **Random Outfit** off, the outfit dropped becomes the one worn, taking the place of the one it replaces, which leaves the list; an outfit already on the list, left there by **Random Outfit**, just becomes the one worn. Turning **Random Outfit** off keeps on the outfit last picked (the first of the list when none was picked yet); the others stay on the list, hidden, for the next time it is on. A folded heading opens after a drag has hovered it for a second. The selected situation's details show its **Name** (the Default's cannot change, which it says on hover; an empty name, or one another situation has, ignoring case, gets its previous name back), its conditions, which cannot be edited yet, and **Random Outfit (for Lydia)**, whose `(?)` explains it. **Show Outfit List** shows the outfit list again, the situation still selected. **Remove**, at the right end of that button's row (not on the Default), asks first, listing who has outfits for it in this save (`Lydia: 2 outfit(s)`), then removes it from `situations.json` and clears it for everyone in this save, redressing those wearing it as **Remove** on Manage Wardrobe does. **Restore Presets** at the right end of the buttons, a link as that **Remove** is with a `(?)` that explains it, greyed while every situation is as installed, lists what it restores and asks, then brings back a removed situation, empty, and the installed names; nobody's outfits change. Everything else is an edit until **Save Changes**, greyed until something changed: it saves the names to `situations.json`, then the character's situations, dressing them at once as Manage Wardrobe's **Update** does (with Live Preview on, an outfit whose details show stays on the preview); a refused save changes nothing and shows why. The preview shows the Inventory page's ticks here while no outfit's details show. **Exit** or Esc closes the window, first asking about unsaved edits.
- **Edit** (from Manage Wardrobe) — the Inventory page's item list with the outfit's edited items ticked, those nobody carries under their own heading, and its own ticks and **Live Preview** tick. The item count ends in **Add items from Wardrobe...**, as on the Inventory page: **Load** there ticks the chosen outfit's items beside this page's ticks, unticking those that share a body slot with them, and returns. **Use Ticked Items** makes the ticks the outfit's edited items and returns; **Back** or Esc returns without them. Nothing is saved here.

On the outfit pages (Load Outfit, Update Existing Outfit, Manage Wardrobe and Manage Situations' outfit list), the item count includes items from plugins that are not loaded and says how many those are; they have no row in the list, and they stay in the outfit. The selected outfit is shown on the live preview, on Manage Wardrobe as edited, and clicking it again deselects it. Deselecting it, or leaving the page, shows the ticked items again.

The search field (magnifying glass) above the list narrows it; each outfit page keeps its own search and order. The search keeps the outfits whose name contains the typed text, ignoring case; while it narrows the list it is underlined in the accent colour, its cross clears it, and Esc clears it before it goes back. Click the **NAME** label over the list to sort it by name, or the arrows left of it, over the icons, to sort it by icon (in the order of Manage Wardrobe's icon picker, then by colour); click again to reverse the order. The list opens sorted by name, and the outfit count is under it. Each name follows the outfit's icon, which you choose on Manage Wardrobe. Every second row is shaded faintly in the palette's `Stripe` colour, as in Tailor's outfit list. At a row's right end, a dressed form marks the outfit the character wears now, a bare form their Default when they are not wearing it, and a star one chosen for a situation; hover them for `Current Outfit` (with `Default Outfit` when it is their Default too), `Default Outfit` and `For Town, Warm`. A selected outfit stays selected, and named on the right, while the list hides it.

The result is shown as a HUD message. The plugin writes nothing until you confirm, and it asks before every update and every removal, the steps that cannot be undone.

### Live Preview

While **Live Preview** is ticked, the game keeps running and the character wears the ticked items, the outfit selected on the Load Outfit page or on Update Existing Outfit, the edited outfit on Manage Wardrobe, or the Edit page's ticks, seen through a free camera. With it off the game is paused and the view stays as it was. The window opens with the preview on or off as `LivePreview` says (see [Settings](#settings)); the tick switches it at any time.

With the cursor over the game rather than the window:

| Input         | Camera                                                                  |
| ------------- | ----------------------------------------------------------------------- |
| W / A / S / D | Moves; hold Shift for half speed. Ignored while a text field is active. |
| Left drag     | Sideways turns the view around the character; up and down tilts it.     |
| Right drag    | Pans.                                                                   |
| Mouse wheel   | Zooms.                                                                  |

`SpeedPercent` scales all four.

The preview dresses every character through their inventory: items they carry are put on, items they lack are added for the preview only. Every close of the window, a save included, ends the preview, takes those copies back, and the character goes back to the very items they wore when it opened, so armor the game picked at random for them, such as a bandit's, stays as it was. An outfit Livery keeps on an NPC is left alone by the preview and is back on when it ends.

### Which items are listed

Every item of armor from a plugin that the character wears or carries. Non-playable armor, such as the physics objects of body mods, is left out. Player-enchanted gear is saved as the plugin armor it was made from; other runtime-created armor has no plugin record and is left out. Items from plugins that are not loaded never show up.

### Outfit assignments

Livery dresses a character without changing their outfit record. **Apply** on the Inventory page dresses the character in the ticked items as their Default, replacing an outfit Livery gave them before; load a saved outfit first to dress them in it. The window stays open, and the Inventory page then lists and ticks what they wear; the character shows the change at once, Live Preview on or off. When Livery has a saved outfit on them and the ticks differ from it, Apply first asks `Update "Jarl's Finery" with the ticked items?` (naming the situation when they wear it for one): **Yes** saves the ticks into that outfit and dresses them in it, for its situation too, without touching their Default; **No, apply as is** (hover it for its help) makes the ticks their Default as they are, without adding them to the wardrobe, which a situational outfit they wear covers until the situation ends; **Cancel** goes back. **Restore** ends it.

On an NPC, the ticked items under `<your name>'s Inventory` leave your inventory at Apply and go to them, enchantments and tempering included, the way the container menu moves an item: an unworn copy first, the enchanted or most tempered one when you carry several, and the one you wear only with `ShowPlayerWornPieces` on and no other. They are the NPC's own from then on and stay with them after Restore. The message on screen counts the items moved; one that could not be moved is left out of the dressing, never replaced by a copy. With `MoveFromPlayer` off, the NPC gets a new copy instead and yours stays. **Release everyone**, at the end of SKSE Menu Framework's **Livery** page, releases everyone Livery dresses after a popup asks again: do this before you uninstall Livery, with a save loaded and the window closed. Without SKSE Menu Framework, Restore releases one character at a time.

For each item, Livery uses a copy the character already carries: the one they wear, else an enchanted one, else the most tempered one, else a plain one, so enchantments and tempering carry over. An NPC who carries none gets a new plain copy. You get one only when `AddMissingPieces` is on, because that creates items; otherwise the message names the items you do not carry. Once an item is on, worn armor outside the outfit comes off, except non-playable armor and items another mod locked. When no item can be put on, the character keeps what they wear.

An NPC keeps the items on: each is locked as with the game's PreventRemoval, so the NPC no longer swaps it for better armor you give them. The lock is saved with the item. When another mod takes an item off, Livery puts it back on the next time the NPC's 3D loads, as long as they still carry it. The copies Livery added do not show in the trade menu, as an NPC's own outfit does not; items that were theirs or that you gave them do, and an item you take back there stays with you. Your own items are never locked, since a locked item cannot be taken off in the inventory.

Restoring a character's original outfit removes the locks, takes back the copies Livery added, and puts back on their own items that Apply took off. Items that were theirs, or that you gave them, stay, worn or not, so a character dressed in gear they already owned keeps it on. A slot whose own item is gone is filled when the game next re-evaluates the NPC, which can take a cell change or two. When an NPC dies, Livery removes the locks and forgets them; the copies stay on the corpse and nothing is put back on.

Assignments live in the SKSE co-save, so each save has its own. Livery adds no ESP, so uninstalling it cannot break a save: the locks and copies left on characters are ordinary item data. Releasing everyone first removes those too, except on NPCs out of reach (not persistent, and in a cell that is not loaded).

### Situations

A character, an NPC or you, can have an outfit for a situation: **Town** (a city, town, settlement, inn or other inhabited interior), **Home** (a house), **Sleep** (going to bed, in it or getting up), **Swimming** (in water; on horseback only swimming counts, not wading) or **Warm** (outdoors in cloudy, rainy or snowy weather, or anywhere outdoors in a snowy region). Drag the outfit onto the situation on Manage Situations, or press **Apply to Lydia** on Manage Wardrobe (or **Create and Apply** on the Save page) while they wear an outfit for that situation: a situational outfit is a saved outfit, taken from the wardrobe each time it goes on, so updating it there changes what they wear next time. A situation lists outfits per character and wears one of them: the one chosen, or with **Random Outfit** on (Manage Situations) one picked at random each time the situation begins, kept until it ends. **Apply to Lydia** and **Create and Apply** ask first before they give a situation another outfit. The outfit assigned for **Default** is worn where no situation applies; a fight is always fought in it, a home without a Home outfit uses the Town one. A character with situational outfits but no Default wears their own clothes outside them.

Each situation has a name, which the window and the HUD show: `Wearing "Jarl's Finery" for Town.` The names are kept in `Data\SKSE\Plugins\Livery\situations.json`, shared by every save and character, which lists only the situations that differ from the installed ones; without it, every situation is as installed, and Livery does not create it until a situation changes. Each entry has the situation's `key` (`Default`, `Town`, `Home`, `Sleep`, `Swimming` or `Warm`), its `name` and `removed`; other fields are ignored. Manage Situations changes it; you can also edit it with the game closed. A removed situation is offered nowhere and nothing is worn for it; a save that gave it outfits drops them when it loads. The Default cannot be renamed or removed, and no two situations may share a name, ignoring case. An empty file counts as no file. A file that cannot be read leaves every situation as installed and is never overwritten; `Livery.log` says why.

Livery checks every second and redresses the character when their situation changes, one character per second when several change at once; nothing changes while the window is open. The line under the name shows what they wear now and the situation it is for. With `NotifyChanges` on, a HUD message says who changed into what. Dragging an outfit off a situation on Manage Situations, then **Save Changes**, removes it from that situation; Restore clears everything, situations included. Your situational outfits are never locked, like your Default, and take only items you carry unless `AddMissingPieces` is on. Nothing changes while you are a werewolf or Vampire Lord; once you turn back, Livery changes your outfit only when your situation calls for another one.

### Settings

Settings live in `Data\SKSE\Plugins\Livery.ini`, which the plugin creates with defaults on first start (keys a newer version adds are appended, so an update never resets your settings). They are read once when the game starts. With SKSE Menu Framework installed, the same settings are on its **Livery** page and take effect at once; **Restore default settings** there puts every one but the palette back to its default, after asking.

| Section      | Key                            | Default  | Meaning                                                                                                |
| ------------ | ------------------------------ | -------- | ------------------------------------------------------------------------------------------------------ |
| `General`    | `RestrictToFollowersAndSpouse` | `1`      | `1` limits the hotkey to followers and your spouse; `0` allows any adult NPC.                          |
| `General`    | `LivePreview`                  | `1`      | `1` opens the window with Live Preview on; `0` opens it with Live Preview off and the game paused.     |
| `General`    | `NpcReaction`                  | `1`      | `1` has the NPC turn to you and say a greeting when the window opens with Live Preview on.             |
| `UI`         | `Palette`                      | `Modern` | Look of the window: a file in `Livery\palettes\`, named without `.ini` (see Palettes).                 |
| `Hotkey`     | `Key`                          | `45`     | The hotkey as a DirectInput scan code (`45` = X). In game, click the hotkey button and press a key.    |
| `Hotkey`     | `Modifier`                     | `42`     | Key to hold together with `Key`: `0` none, `42` Shift, `29` Ctrl, `56` Alt (left or right).            |
| `Camera`     | `SpeedPercent`                 | `100`    | Speed of the preview camera's movement, drags and wheel zoom, in percent (`10` to `200`).              |
| `Player`     | `AddMissingPieces`             | `0`      | `1` lets an outfit you give yourself add the items you do not carry, which creates items; in game the tick **Dress yourself from inventory only** is its opposite. |
| `Window`     | `ShowPlayerInventory`          | `1`      | `1` lists your armor in an NPC's window, under `<your name>'s Inventory`; never quest items.           |
| `Window`     | `ShowPlayerWornPieces`         | `0`      | `1` lists the armor you wear there too; `0` leaves it out unless you carry another copy; in game the tick **Exclude equipped items** is its opposite. |
| `Window`     | `ShowSlotColumn`               | `0`      | `1` adds a Slot column to the window's item list: each item's body slots by number.                    |
| `Transfer`   | `MoveFromPlayer`               | `1`      | `1`: Apply moves the ticked items of that list to the NPC; `0` gives the NPC new copies.               |
| `Transfer`   | `ProtectFavorites`             | `1`      | `1` leaves your favorites out of that list.                                                            |
| `Situations` | `NotifyChanges`                | `0`      | `1` shows a HUD message when a character changes outfit because a situation began or ended.            |

The wardrobe, Livery's outfit library, is `Data\SKSE\Plugins\Livery\outfits.json`, shared by every save and character; each item is stored as its plugin name and FormID, so an outfit survives a load order change, and each outfit keeps its icon and colour. `situations.json` beside it (see [Situations](#situations)) is shared the same way. Back them up with your saves. The outfit categories older versions wrote to `outfits.json` are ignored and gone at the next save. An empty file counts as no file; one that cannot be read leaves the wardrobe empty and is never overwritten, and `Livery.log` says why.

Skins: the palette (see Palettes) picks the skin and its colours. **Modern**, the default, draws dark panels with nearly square frames after [Edge UI](https://www.nexusmods.com/skyrimspecialedition/mods/130983), in the Jost font, its labels grey and a selected row filled in the selected colour, fading out towards its right end, with a rule in the accent colour at its left edge; its colours come from the palette. **Vanilla** mimics the game's menus, with a light band round the window and grey text buttons that turn white when you point at them; **Nordic** mimics NORDIC UI, with its corner knot and heading rope, and marks a hovered row with NORDIC UI's list line. Both mark the selected outfit with the equipped mark of a ticked item, draw the rule after a list heading as SkyUI's MCM header rule with its knot and lists' scroll bars as the game's journal scroll bar, and draw text fields and dropdowns as a rule under the text: a text field has a pen at its right end, and a dropdown's arrow is grey until you point at it. The skins' fonts and textures ship in `Data\SKSE\Plugins\Livery\`, with the licences of the fonts and textures.

Palettes: each `.ini` in `Data\SKSE\Plugins\Livery\palettes\` sets the window's look: its `Skin` (`Modern`, `Vanilla` or `Nordic`; Modern when left out) and that skin's colours. Pick one in `Palette` or in the **Window skin** dropdown on the SKSE Menu Framework page, which lists each by its skin and file name (`Modern - Edge`), or by the skin alone for the skin's own palette; it takes effect the next time the window opens. Livery ships **Modern** (a general-purpose dark grey that suits Untarnished UI and [Norden UI](https://www.nexusmods.com/skyrimspecialedition/mods/166086)), **Modern - Edge** (dark, with a muted gold accent, after Edge UI), **Vanilla** and **Nordic**. A skin reads only the colours it uses: Vanilla every one but `AccentLight`, Nordic every one but `AccentLight` and `Selected`; Livery writes only those into its palettes and ignores the others if you add them. The game's art (the scroll bar, the checkbox, the equipped marks) keeps its own colours. Livery writes each of these files only when it is missing, so an update never undoes your edits; **Restore original palette** appears beside the dropdown while the chosen shipped palette's skin or colours differ from what Livery ships, and writes it back as shipped, after asking. To make your own, copy one under a new name in that folder and change its colours, each `#RRGGBB` or `#RRGGBBAA` (`AA` the opacity) with a comment saying what it colours; it is listed the next time the game starts, and the file can be shared as it is. A colour left out or written otherwise keeps the skin's own, and `Livery.log` names one written otherwise.

### Limits

- Only ASCII can be typed into the name field (the game's text input carries the ANSI code page).
- Items from plugins that are not loaded have no row in the window, and an update leaves them in the outfit. Edit `outfits.json` to remove them.
- The window has no delete. Edit `outfits.json` for that, with the game closed.
- Earlier versions, as a Tailor add-on, kept a working outfit named `Preview Outfit (used by Capture Outfit add-on)` in Tailor's library. It is no longer used; delete it in Tailor.
- Slots an assigned outfit leaves empty stay the game's: an NPC can put on a helmet or shield they own there.
- Updating an outfit changes another character's Default only when you give it to them again: a Default keeps the items it was given.
- Your Sleep outfit goes on only with a mod that puts you into bed: the game's own beds just pass the time.
- The copies Livery adds to an NPC who has no default outfit show in the trade menu, where you can take them for free; so do those on any game version where `Livery.log` reports a trade-menu call site it could not find.

Details go to `Documents\My Games\Skyrim Special Edition\SKSE\Livery.log` (`Skyrim Special Edition GOG` for the GOG version).

---

## Development

### Prerequisites

#### All platforms

- [Git](https://git-scm.com/)
- [CMake](https://cmake.org/download/) 3.21+
- The `vcpkg` and `CommonLibSSE-NG` submodules: clone with `--recurse-submodules`, or run `git submodule update --init --recursive`

#### Windows

- [Visual Studio 2022 or newer](https://visualstudio.microsoft.com/) (or the standalone Build Tools) with **Desktop development with C++**, which provides MSVC, CMake, and Ninja

#### Linux (cross-compilation)

- LLVM/Clang (provides `clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc`, `llvm-mt`)
- [xwin](https://github.com/Jake-Shadle/xwin) — downloads the real Windows SDK and MSVC CRT headers/libs
- [Ninja](https://ninja-build.org/)

```bash
# Arch / CachyOS
sudo pacman -S clang lld llvm ninja

# Install xwin (requires Rust/cargo)
cargo install xwin

# Fetch Windows SDK + MSVC CRT headers to ~/.xwin  (one-time, ~700 MB)
xwin splat --output ~/.xwin
```

> **Note:** On first configure, `cmake/toolchains/clang-cl-cross.cmake` creates
> TitleCase symlinks inside your xwin installation, e.g.:
>
> ```
> ~/.xwin/sdk/lib/um/x86_64/Advapi32.lib  ->  advapi32.lib
> ```
>
> lld-link is case-sensitive but CommonLibSSE-NG references libs with mixed-case names.
> The originals are untouched.

---

### Build

#### Windows (MSVC)

```bash
cmake --preset release-windows
cmake --build --preset release-windows
```

> Run these from a **Developer Command Prompt for VS** (or after sourcing `vcvars64.bat`) so `cl.exe` and the Windows SDK are on `PATH`. The preset uses the Ninja generator, which builds with whatever MSVC toolset the environment provides.

The DLL lands in `build/msvc/livery.dll`.

In VS Code, `.vscode/tasks.json` runs the same commands inside the VS developer environment; the default build task (`Ctrl+Shift+B`) builds and deploys in one go.

#### Linux (clang-cl + xwin)

```bash
./scripts/build.sh
# or directly:
cmake --preset release-linux
cmake --build --preset release-linux
```

The DLL lands in `build/release-linux/livery.dll`.

#### Deploy to a mod manager

The `deploy` target copies `livery.dll`, its PDB and the `Data/SKSE/Plugins/Livery` folder (skin fonts and textures) into `<deploy dir>/SKSE/Plugins/`. No INI or palette is deployed; the plugin creates them on first start.

The deploy directory is, in order: `-DDEPLOY_DIR_OVERRIDE=<path>` given at configure time; `$SKYRIM_MODS_FOLDER/livery` (your mod manager's staging folder); `$SKYRIM_FOLDER/Data`; else `build/<preset>/deploy/`. Copy `.env.example` to `.env` to set the environment variables.

```bash
# Windows: pin the destination once, then build the deploy target
cmake --preset release-windows -DDEPLOY_DIR_OVERRIDE="C:/path/to/MO2/mods/Livery"
cmake --build --preset release-windows --target deploy

# Linux (deploy.sh sources .env for you; cmake does not):
./scripts/deploy.sh
# or: source .env && cmake --workflow --preset deploy
```

### Running tests

Unit tests build without CommonLibSSE, so they run as a native executable. Two presets are available:

- **Windows** (`test-windows`) uses Catch2 from vcpkg.
- **Linux** (`test-linux`) uses the system Catch2 3 package. Install it with `sudo pacman -S catch2` on Arch/CachyOS, or your distribution's equivalent.

```bash
cmake --preset test-windows
cmake --build --preset test-windows
ctest --preset test-windows
```

Tests live in `test/` and use [Catch2](https://github.com/catchorg/Catch2). Only pure-logic code (no RE::/SKSE:: APIs) can be tested this way. See `src/Utils.h` and `test/ExampleTests.cpp` for the pattern.

---

### Git Hooks (Lefthook)

Prerequisites:

- `go install github.com/evilmartians/lefthook@latest`
- `clang-format` (part of LLVM — already required for development)
- `clang-tidy` (part of LLVM — already required for development)
- `cmake-format` (`sudo pacman -S cmake-format` on Arch/CachyOS; `pip install cmakelang` elsewhere)
- `shellcheck` (`sudo pacman -S shellcheck` on Arch/CachyOS)

```bash
lefthook install
```

---

### Editor Setup (clangd / Neovim)

CMake writes `compile_commands.json` to the build directory automatically.
Copy or symlink it to the project root so clangd picks it up:

```bash
# After configuring:
ln -sf build/release-linux/compile_commands.json compile_commands.json
```

The `.clangd` file already sets `--target=x86_64-pc-windows-msvc` so clangd
resolves Windows headers correctly on Linux.

Recommended Neovim plugins: [nvim-lspconfig](https://github.com/neovim/nvim-lspconfig)
with `clangd`, and [clangd_extensions.nvim](https://github.com/p00f/clangd_extensions.nvim).

---

### Updating CommonLibSSE-NG

The script pins the submodule to a release tag (`v*`) on the `ng` branch and stages it. Without an argument it picks the latest tag:

```bash
./scripts/update.sh          # latest release tag
./scripts/update.sh v10.1.0  # a specific tag
git commit -m "chore(deps): update CommonLibSSE-NG to v10.1.0"
```

### Nexus Mods Page

The `<!-- nexus:start/end -->` block at the top of `README.md` is the **source of truth** for Requirements, Installation, and Compatibility. `docs/nexus-page.md` holds the rest of the BBCode page (overview, tagline, credits).

To update the Nexus page:

1. Edit Requirements/Installation/Compatibility inside the `<!-- nexus:start/end -->` block in `README.md`.
2. Edit overview, tagline, and credits directly in `docs/nexus-page.md`.
   > **Do not edit** the `<!-- generated:start/end -->` block in `docs/nexus-page.md` — the script replaces it with the README block.
3. Generate the combined BBCode output:

```bash
python3 scripts/generate-nexus-page.py

# Or copy straight to the clipboard (wl-copy, xclip, xsel or pbcopy):
python3 scripts/generate-nexus-page.py --copy
```

4. Paste the output into the Nexus Mods page editor.

---

### CI

| Workflow           | Trigger                                                                                               | What it does                                                                                                                              |
| ------------------ | ----------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `ci.yml`           | PRs to `main` touching `src/`, `test/`, `cmake/`, `vcpkg.json`, `CMakeLists.txt`, `CMakePresets.json` | `clang-format` (ubuntu) → `test-linux` and `build-linux` (ubuntu) and `test-windows` + `build-windows` + `clang-tidy` (windows, parallel) |
| `release.yml`      | Push of a `v*` tag                                                                                    | Builds, packages via `scripts/package.sh`, publishes a GitHub Release with zip + PDB                                                      |
| `nexus-upload.yml` | Auto-triggered via `workflow_run` once `release.yml` completes, or manual `workflow_dispatch`         | Downloads release zip, generates cliff release notes, uploads to Nexus Mods                                                               |
| `lint.yml`         | PRs touching `scripts/`, `README.md`, or `docs/`                                                      | Runs shellcheck on shell scripts, plus dprint formatting and Vale prose checks on Markdown                                                |
| `pr-title.yml`     | PR opened/edited/reopened/synchronize                                                                 | Checks PR title follows Conventional Commits (`feat`, `fix`, `chore`, `refactor`)                                                         |

`setup.yml` is left over from the repository template; it only runs in a repo newly created from a template.

#### Nexus Mods Upload

`nexus-upload.yml` runs once `release.yml` completes, after approval in the `nexus` environment. It can also be run manually via **workflow_dispatch** with a version input. The one-time setup (environment, secrets, and variables) is in [docs/RELEASING.md](docs/RELEASING.md#nexus-mods-upload).

---

## License

Copyright (c) 2026 Livery Contributors. Licensed under the GNU GPL v3.0 or later; see [LICENSE](LICENSE). CommonLibSSE-NG is also GPL-3.0-or-later. The SKSE Menu Framework header in `lib/extern/` is LGPL-2.1; the Jost, Oswald and Sovngarde fonts are under the SIL Open Font License; the Nordic skin's ornaments are NORDIC UI art by Dopesan, used under the permissions on its mod page; the list heading rule is SkyUI art by the SkyUI Team, used under the permissions on its mod page (see `Data/SKSE/Plugins/Livery/textures/SKYUI-CREDITS.txt`); the item icons are SkyUI art by PsychoSteve and Jelidity, used with their permission (see `Data/SKSE/Plugins/Livery/icons/ICON-CREDITS.txt`).
