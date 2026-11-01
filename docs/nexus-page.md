# Nexus Mods Page Content

## Tags

- SKSE
- Utilities for Players

---

## Long Description (BBCode)

<!-- The Requirements, Installation, and Compatibility sections below are generated.
     Run `python3 scripts/generate-nexus-page.py` and copy the output to Nexus Mods.
     Do not copy this file directly — the generated block markers are not valid BBCode. -->

```bbcode
[center][size=5][b]Livery - Outfit Manager[/b][/size]

[i]A lightweight, intuitive outfit manager designed with immersion and fair gameplay in mind.[/i][/center]

Livery lets you manage what you and your NPCs wear, save their current equipment as outfits, and switch outfits automatically to suit the situation.

Built as an SKSE plugin, it pairs a streamlined interface with a focus on performance, simplicity and respect for Skyrim's own rules.

[line]

[size=4][b][color=#B8953E]Features[/color][/b][/size]

[b]Dress NPCs[/b]
Change an NPC's equipment through a dedicated interface, using items from their inventory or yours. What you put on stays on: no swapping it for the "better" armor you asked them to carry, no drifting back to their old outfit.

[b]Save Outfits[/b]
Save what you or an NPC are wearing as an outfit, and put it on anyone, any time, from a Wardrobe shared by all your characters. No rebuilding it piece by piece, and your enchanted or tempered copies are used when you have them.

[b]Situational Outfits[/b]
Give a character a different outfit for Town, Castle, Home, Sleep or Cold (rain and snow outdoors). Livery changes them automatically, and remembers it per character.

[line]

[size=4][b][color=#B8953E]Design Philosophy[/color][/b][/size]

Livery is built on three principles: simplicity, immersion and fair gameplay.

[b]Simple and Streamlined[/b]
[list]
[*]Intuitive interface: aim, press the hotkey, tick the items, Apply.
[*]Lightweight by design: an SKSE plugin only. No ESP or ESM in your load order, no Papyrus scripts in the background, nothing that can corrupt a save.
[/list]

[b]Immersion-Conscious[/b]
Convenient outfit management shouldn't feel like stepping out of Skyrim.

[i]An interface that belongs in Skyrim[/i]
[list]
[*]Three skins: Vanilla, NORDIC UI and Modern.
[*]Editable colour palettes, with presets such as an Edge UI-inspired scheme.
[*]A compact window that keeps the NPC in view instead of hiding them behind a full-screen menu.
[*]Open Livery on an NPC and they turn to you and say hello. (Configurable)
[/list]

[i]Respect for the game's rules[/i]
[list]
[*]By default, Livery dresses only you, your followers and your spouse. Friends you can't dress, but you can save what they wear. (Configurable)
[*]An outfit is built from equipment that actually exists in someone's inventory, or from the NPC's current outfit, never from a plugin's item list.
[*]By default, you can only wear items you actually carry. (Configurable)
[/list]

[i]No free items, no lost equipment[/i]
[list]
[*]Saving or switching an outfit never consumes an item.
[*]Items Livery creates for an NPC's outfit cannot be taken from them through trading.
[/list]

[b]Your Game, Your Rules[/b]
Livery's defaults lean towards immersion, but they're not there to tell you how to play. Everything marked (Configurable) is a setting: tighten the rules or loosen them.

[line]

[size=4][b][color=#B8953E]Quick Start[/color][/b][/size]

[b]Configure Livery[/b]
Every setting is on the Livery page of SKSE Menu Framework, where it takes effect at once, or in [font=Courier New]Data\SKSE\Plugins\Livery.ini[/font], read when the game starts. The window's Appearance tab has the skin, palette and menu sounds, with or without SKSE Menu Framework.

[b]Opening Livery[/b]
Press the hotkey (Shift + X by default). Aim at an NPC to open Livery on them; aim at nothing to open it on yourself.
What the window lets you do depends on who they are. By default, you can dress your followers and spouse, save what your friends wear, and nothing else. (Configurable)
Dressing yourself uses only items you carry. A setting lifts that, but it's not recommended for normal play.

[b]Dress a follower or your spouse[/b]
[list=1]
[*]Open Livery on the NPC.
[*]Tick the items you want them to wear.
[*]Apply, then Exit.
[/list]
Add from Wardrobe puts a saved outfit's items on the list. Items Livery put on stay on.

[b]Save an outfit[/b]
[list=1]
[*]Dress the character in what you want to save, or open Livery on a friend wearing something you like.
[*]Press Create / Update Outfit.
[*]Name it, pick an icon, Create.
[/list]
To change an existing outfit, use the Update tab. A friend's window only saves what they wear; it can't change it.

[b]Wear an outfit from the Wardrobe[/b]
[list=1]
[*]Pick the Wardrobe tab on the left edge of the window.
[*]Select an outfit.
[*]Apply, then Exit.
[/list]
Rename an outfit, change its icon or remove it on the same page.

[b]Set situational outfits[/b]
[list=1]
[*]Pick the Situations tab on the left edge of the window.
[*]Drag outfits from the list on the right onto the situations on the left. They are saved as you go.
[/list]
Drag an outfit back to the list to clear it. Situational outfits are saved per character.

[b]Keep variants in a chest[/b] (optional: turn on Item chests in the settings)
[list=1]
[*]Pick the Chests tab, Create New Chest, and click it to fill it: the visor open and closed, a wig in each colour.
[*]When dressing someone, use Add from Chest under the list and Load the chest.
[*]Tick the variant you want, Apply.
[/list]
A chest is never worn as a whole, so it needs no outfit per variant. Chests are shared by every save.

[line]

<!-- generated:start -->
[size=4][b][color=#B8953E]Requirements[/color][/b][/size]

[list]
[*][url=https://skse.silverlock.org/]SKSE64[/url]
[*][url=https://www.nexusmods.com/skyrimspecialedition/mods/32444]Address Library for SKSE Plugins[/url] for SE and AE
[*]Optional: SKSE Menu Framework, for changing the settings in game
[/list]

[line]

[size=4][b][color=#B8953E]Installation[/color][/b][/size]

[b]Mod manager (recommended):[/b]
[list=1]
[*]Install the requirements above.
[*]Install Livery via your mod manager.
[*]Launch Skyrim via SKSE.
[/list]

[b]Manual:[/b]
[list=1]
[*]Install the requirements above.
[*]Copy the [font=Courier New]SKSE[/font] folder from the archive into [font=Courier New]Data\[/font] (it holds [font=Courier New]livery.dll[/font] and the fonts, textures and palettes of the window skins).
[*]Launch Skyrim via SKSE.
[/list]

[line]

[size=4][b][color=#B8953E]Compatibility[/color][/b][/size]

[list]
[*]Skyrim SE and AE. Skyrim VR is not supported.
[*]No ESP/ESL. Livery will not corrupt a save or set your PC on fire next to any other mod: no scripts, and its co-save record is skipped when it is gone. A second outfit manager on the same character gets confusing, and a second mod switching outfits by situation more so. TL;DR: unless you have a reason to, don't run it beside another outfit management mod.
[*][b]Nether's Follower Framework[/b]: fine, as long as you don't use its outfit features on the followers Livery dresses.
[*][b]Tailor[/b]: fine for wigs; leave the outfits to one of the two.
[*][b]Devious Devices[/b]: not tested. Don't dress a character wearing devices.
[/list]
<!-- generated:end -->

[line]

[size=4][b][color=#B8953E]Uninstall[/color][/b][/size]

Safe to uninstall at any time: Livery adds no plugin file, so removing it can't break your save. NPCs you dressed keep their outfit locked on and the items Livery gave them become lootable; press [b]Release everyone[/b] on the settings page first to undo that.

[line]

[size=4][b][color=#B8953E]FAQ[/color][/b][/size]

[b]An NPC changed back after a bath or a scene.[/b]
By design. Livery puts the outfit back on when the NPC's 3D loads or their situation changes, not every frame, so other mods can undress them for a while.

[b]My character won't wear the outfit.[/b]
You can only wear items you carry; the message on screen names the missing ones. Untick "Dress yourself from inventory only" to let Livery add them.

[b]Does Livery keep my enchantments and tempering?[/b]
An outfit stores only which items it has. When it goes on, Livery picks the best copy the character carries: the one they wear, else a player-enchanted one, else the most tempered one, else a plain one. The same goes for items you hand to an NPC. Enchanted items from a plugin, like a Ring of Alchemy, are items in their own right and keep their effect.

[b]My Sleep outfit never goes on.[/b]
The game's own beds just pass the time. It goes on with a mod that actually puts you into bed.

[b]Where are my outfits stored?[/b]
[font=Courier New]Data\SKSE\Plugins\Livery\outfits.json[/font], shared by all saves. Items are stored by plugin name and FormID, so outfits survive a load order change. Back it up with your saves.

[b]Can I type a non-English name?[/b]
Only ASCII can be typed into the name field for now (see Roadmap).

[b]Something went wrong. Where do I look?[/b]
[font=Courier New]Documents\My Games\Skyrim Special Edition\SKSE\Livery.log[/font].

[line]

[size=4][b][color=#B8953E]Roadmap[/color][/b][/size]

[b]In progress[/b]
[list]
[*]Custom situations: add your own, and edit their conditions.
[*]Non-ASCII names, so you can name an outfit in your own language.
[*]Livery API, so other mods can use its dressing and outfits.
[/list]

[b]Ideas, not planned (ask, and they move up)[/b]
[list]
[*]Partial outfits that go on over the current one, e.g. a hood and cloak for cold weather. Handy, but a second kind of outfit to keep track of. For now, save a copy of an outfit with the hood and cloak added and give it to Cold.
[*]Tags on outfits, like Light Armor or Casual. Easier to find things, but busier lists. For now, icons and colours are the way to organise.
[*]A wardrobe per save. No outfits from items this character never found, but an outfit saved before a death is lost with the save. For now, move outfits.json aside before a new game.
[*]Opening Livery from dialogue. More immersive, but it needs an ESP.
[*]A middle ground for wearing items you don't own. "Dress yourself from inventory only" is strict; turning it off hands you a free armoury. Something between the two might fit better: items Livery creates could be unsellable and vanish when taken off, or count as stolen, as if you'd lifted the missing ones somewhere. Each is a rule of its own to learn, so: which, if any, would you use? Let me know.
[*]Poses for the NPC while you dress them. Nice for checking the fit, but drifting away from what Livery is for. For now, the greeting turns them to face you.
[/list]

[line]

[size=4][b][color=#B8953E]What Livery Won't Do[/color][/b][/size]

[list]
[*]Pull armor straight out of plugins. Tailor and SOES NG do that well; or run an AddItemMenu-type mod beside Livery.
[*]Enforce an outfit every frame. Livery updates at set moments, so other mods' temporary changes are allowed and the game stays light. If you'd rather have it enforced always, SOES NG is built for exactly that.
[*]Skyrim VR. I don't own it, so I can't test it.
[/list]

[line]

[size=4][b][color=#B8953E]Credits[/color][/b][/size]

[list]
[*]CommonLibSSE-NG, SKSE Menu Framework
[*]Fonts: Jost, Oswald, Sovngarde (SIL Open Font License)
[*]NORDIC UI by Dopesan: the Nordic skin's ornaments
[*]SkyUI Team: list heading rule. PsychoSteve and Jelidity: item icons
[*]Edge UI: the look the Modern skin is modelled on
[/list]

Licensed under GPL v3.

[b]Special thanks[/b]
(to be filled in)
```
