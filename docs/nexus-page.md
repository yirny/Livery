# Nexus Mods Page Content

## Short Description

Aim at a follower, press a key, and the armor they wear becomes a Tailor outfit. Name it, pick its categories and gender in a small window, or put the pieces on an outfit Tailor already has.

---

## Tags

- SKSE
- Utilities for Players

---

## Long Description (BBCode)

<!-- The Requirements, Installation, and Compatibility sections below are generated.
     Run `python3 scripts/generate-nexus-page.py` and copy the output to Nexus Mods.
     Do not copy this file directly — the generated block markers are not valid BBCode. -->

```bbcode
[center][size=5][b]Tailor Outfit Capture[/b][/size]
[i]Turn what an NPC, or you, wear into a Tailor outfit[/i][/center]

[line]

[size=4][b][color=#B8953E]Overview[/color][/b][/size]

Tailor builds outfits from pieces you pick one by one. This plugin skips that: aim the crosshair at a follower or your spouse (or at nothing, for yourself), press [b]F8[/b], and a window lists every piece they wear or carry. From there:

[list]
[*][b]Create New[/b] saves the ticked pieces as a new outfit. The name is pre-filled ([font=Courier New]Captured Outfit 001[/font] and so on), the armor class (Clothing, Light Armor, Heavy Armor) is detected and ticked, the gender is suggested from the pieces, and Tailor's situations and your own custom categories are one click away.
[*][b]Update Existing[/b] puts the ticked pieces on an outfit Tailor already has, after a confirmation, and can add it to more categories.
[*][b]Live Preview[/b] dresses them in the ticked pieces as you click, seen through a free camera you move with W/A/S/D and the mouse.
[*][b]Load Outfit[/b] shows any outfit from Tailor's library on them and ticks its pieces.
[/list]

Wigs are listed but unticked, so a captured outfit does not replace another character's hair. Player-enchanted gear is saved as the armor it was made from.

Apart from the preview's working outfit, [font=Courier New]Capture Preview[/font], nothing is written to Tailor until you confirm, and the plugin never deletes or renames an outfit.

[size=4][b][color=#B8953E]Settings[/color][/b][/size]

[font=Courier New]Data\SKSE\Plugins\TailorOutfitCapture.ini[/font] is created on first start. With [b]SKSE Menu Framework[/b] installed, the same settings are on its Tailor Outfit Capture page:

[list]
[*]Capture only followers and your spouse (default), or any adult NPC.
[*]Hotkey, with an optional Shift, Ctrl or Alt.
[*]Window skin: Tailor (matches Tailor's own menus), Vanilla, Nordic (NORDIC UI) or Classic.
[*]Open the window with Live Preview on (default), or off with the game paused.
[*]Preview camera speed.
[/list]

Only ASCII can be typed into the name field. On Skyrim VR there is no window: the outfit is saved at once under the automatic name. Live Preview cannot run on someone another mod has set an outfit override on. On yourself, unticking the last piece leaves it shown, as Tailor does not dress the player in an empty outfit; the saved outfit follows the ticks. [font=Courier New]Capture Preview[/font] stays in Tailor's library, since Tailor's mod API cannot delete an outfit.

[line]

<!-- generated:start -->
[size=4][b][color=#B8953E]Requirements[/color][/b][/size]

[list]
[*][url=https://skse.silverlock.org/]SKSE64[/url]
[*][url=https://github.com/heathbrownkeyworks/Tailor]Tailor[/url] 3.0 or newer (mod API 1.0)
[*][url=https://www.nexusmods.com/skyrimspecialedition/mods/32444]Address Library for SKSE Plugins[/url] for SE and AE, or [url=https://www.nexusmods.com/skyrimspecialedition/mods/58101]VR Address Library for SKSEVR[/url] for VR
[*]Optional: SKSE Menu Framework, for changing the settings in game
[/list]

[line]

[size=4][b][color=#B8953E]Installation[/color][/b][/size]

[b]Mod manager (recommended):[/b]
[list=1]
[*]Install the requirements above.
[*]Install Tailor Outfit Capture via your mod manager.
[*]Launch Skyrim via SKSE.
[/list]

[b]Manual:[/b]
[list=1]
[*]Install the requirements above.
[*]Copy the [font=Courier New]SKSE[/font] folder from the archive into [font=Courier New]Data\[/font] (it holds [font=Courier New]tailoroutfitcapture.dll[/font] and the fonts and textures of the window skins).
[*]Launch Skyrim via SKSE.
[/list]

[line]

[size=4][b][color=#B8953E]Compatibility[/color][/b][/size]

[list]
[*]Compatible with Skyrim SE, AE, and VR. On VR there is no capture window: the outfit is saved under an automatic name.
[*]No ESP/ESL required.
[/list]
<!-- generated:end -->

[line]

[size=4][b][color=#B8953E]Credits[/color][/b][/size]

[list]
[*][url=https://github.com/heathbrownkeyworks/Tailor]Tailor[/url] and its mod API by heathbrownkeyworks
[*][url=https://skse.silverlock.org/]SKSE[/url] by the SKSE Team
[*][url=https://www.nexusmods.com/skyrimspecialedition/mods/32444]Address Library for SKSE Plugins[/url] by [url=https://www.nexusmods.com/profile/meh321]meh321[/url]
[*][url=https://github.com/alandtse/CommonLibSSE-NG]CommonLibSSE-NG[/url] by [url=https://github.com/alandtse]alandtse[/url] and contributors
[*]SKSE Menu Framework, whose API header this plugin uses for its settings page
[*][url=https://github.com/ocornut/imgui]Dear ImGui[/url] by Omar Cornut and contributors
[*][url=https://www.nexusmods.com/skyrimspecialedition/mods/49881]NORDIC UI - Interface Overhaul[/url] by Dopesan: the Nordic skin's corner knot and heading rope, used under the permissions on that page
[*][url=https://github.com/googlefonts/OswaldFont]Oswald[/url] by the Oswald Project Authors and [url=https://mjorka.net]Sovngarde[/url] by mjorka, both under the SIL Open Font License
[/list]
```
