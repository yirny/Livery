# Livery - Immersive Outfit Management

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that saves the armor an NPC, or you, are wearing as an
outfit in its own library, and dresses any character in a saved outfit, keeping an NPC in it. Aim at the NPC (or at
nothing, for yourself), press the hotkey, tick the pieces in a small window, done. No ESP, no Papyrus; removing it
cannot break a save ([design](docs/design.md)).

Built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), [CMake](https://cmake.org) and [vcpkg](https://vcpkg.io).

---

<!-- nexus:start -->

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for SE and AE, or [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101) for VR
- Optional: SKSE Menu Framework, for changing the settings in game

## Installation

**Mod manager (recommended):**

1. Install the requirements above.
2. Install Livery via your mod manager.
3. Launch Skyrim via SKSE.

**Manual:**

1. Install the requirements above.
2. Copy the `SKSE` folder from the archive into `Data\` (it holds `livery.dll` and the fonts and textures of the window skins).
3. Launch Skyrim via SKSE.

## Compatibility

- Compatible with Skyrim SE, AE, and VR. On VR there is no capture window: the outfit is saved under an automatic name.
- No ESP/ESL required.

<!-- nexus:end -->

---

## What the Plugin Does

1. Load a save or start a new game.
2. Aim the crosshair at a follower or your spouse (any adult NPC once `RestrictToFollowersAndSpouse` is off; never a child). With nothing under the crosshair the hotkey captures the player instead.
3. Press the capture hotkey (`[Hotkey] Key`, see [Settings](#settings); F10 by default). The capture window opens and the character wears the ticked pieces (see [Live Preview](#live-preview)); with Live Preview off the game freezes instead. While the window is open the HUD is hidden, the game cannot be saved, and an NPC stays where they stand.

The window has five pages:

- **Pieces** — the heading shows the character's name and, when Livery dresses them, the outfit under it: `Dressed by Livery in "Jarl's Finery".`, or `Dressed by Livery.` for pieces that are not a saved outfit. Under it every armor piece the character wears or carries is listed with a checkbox: worn pieces start ticked except wigs (pieces on the Hair slot only), carried ones unticked. Ticking a piece unticks the ticked ones that share a body slot with it. **Load Outfit...** at the right end of the piece count opens the Load Outfit page; it is greyed while the library has no outfits. The **Live Preview** tick turns the preview on and off. **Assign for** (NPCs only) says what **Assign** is for: **Default**, or a situation (Town, Home, Sleep, Swimming, Warm); for a situation the ticks must be a loaded saved outfit (see [Situations](#situations)). **Situations...** at the right end of that line opens the Situations page; it is greyed while no situation has an outfit. **Create New**, **Update Existing** and **Assign** act on the ticked pieces, so each needs at least one; **Release** acts on the character (see [Outfit assignments](#outfit-assignments)); **Cancel** or Esc closes the window and changes nothing. Throughout the window, a greyed button shows why on hover.
- **Load Outfit** — the library's outfits (left), with the selected one's categories and pieces (right). **Load** ticks exactly its pieces and returns, adding those the character does not carry to the list; it is greyed for an outfit none of whose pieces is loaded. **Back** returns with the ticks unchanged.
- **Name** (after Create New) — the outfit name, pre-filled with the first free `Captured Outfit NNN`; the categories; and the outfit's gender. Press Enter or **Create** to save, **Back** to return to the pieces. An empty name field says so inside the field; when a save fails, the field turns red and shows the reason on hover until you edit the name.
- **Update Existing** — the library's outfits (left), with the selected one's categories and pieces (right). **Update** asks for confirmation, then replaces the outfit's pieces with the ticked ones and its categories with the ticked ones. Its name and gender stay as they are. The confirmation says how many of its pieces come from plugins that are not loaded; those stay in it.
- **Situations** (NPCs only) — the outfit chosen for **Default** and for each situation, with `(worn now)` beside the one the NPC wears. **Clear** marks a situation's outfit to drop and **Undo** takes the mark off; **Apply** drops the marked ones and closes the window; **Back** returns with nothing changed. **Default** has no **Clear**: **Release** ends it.

On both outfit pages, the piece count includes pieces from plugins that are not loaded and says how many those are; they have no row in the list. The selected outfit is shown on the live preview, and clicking it again deselects it. Deselecting it, or going **Back**, shows the ticked pieces again.

The search field (magnifying glass) and the filter dropdown (funnel) above the list narrow it, and the **Sort by** dropdown at the list's top right orders it. The search keeps the outfits whose name contains the typed text, ignoring case. The filter keeps those in the ticked categories: any ticked one within a group, and every group with a tick; a `No ...` row (for example `No Situation`) keeps the outfits in none of that group's categories. **Sort by** orders the list by Name, by Armor Type (then situation, then name) or by Situation (then armor type, then name), following the library's category order with outfits in none of them last. A field that narrows the list is underlined in the accent colour, and its cross clears it; Esc clears the search before it goes back. A selected outfit stays selected, and named on the right, while the list hides it.

The result is shown as a HUD message. The plugin writes nothing until you confirm, and the window never deletes or renames an outfit, so it asks before Update Existing, the one step that cannot be undone.

### Live Preview

While **Live Preview** is ticked, the game keeps running and the character wears the ticked pieces, or the outfit selected on the Load Outfit or Update Existing page, seen through a free camera. With it off the game is paused and the view stays as it was. The window opens with the preview on or off as `LivePreview` says (see [Settings](#settings)); the tick switches it at any time.

With the cursor over the game rather than the window:

| Input         | Camera                                                                  |
| ------------- | ----------------------------------------------------------------------- |
| W / A / S / D | Moves; hold Shift for half speed. Ignored while a text field is active. |
| Left drag     | Sideways turns the view around the character; up and down tilts it.     |
| Right drag    | Pans.                                                                   |
| Mouse wheel   | Zooms.                                                                  |

`SpeedPercent` scales all four.

The preview dresses every character through their inventory: pieces they carry are put on, pieces they lack are added for the preview only. Every close of the window, a save included, ends the preview, takes those copies back, and the character goes back to the very items they wore when it opened, so armor the game picked at random for them, such as a bandit's, stays as it was. An outfit Livery keeps on an NPC is left alone by the preview and is back on when it ends.

### Categories and gender

The armor classes (Clothing, Light Armor, Heavy Armor) are checkboxes; the situations (Adventuring, Town, Home, Sleep, Swimming, Warm) are a dropdown. On the Update Existing page, the categories the outfit is in start ticked. The armor class is detected from the worn pieces (body first, then head, hands, feet, circlet) and ticked for you; no situation is ever ticked by default, so a captured outfit never enters a situation pool unless you put it there.

Gender is suggested the same way: **Female** when the deciding piece has only a female model, **Unisex** otherwise, and never Male unless you pick it. The gender cannot be changed later.

### What gets captured

Every armor piece from a plugin that the character wears or carries. Non-playable armor, such as the physics objects of body mods, is left out. Player-enchanted gear is saved as the plugin armor it was made from; other runtime-created armor has no plugin record and is left out. Pieces from plugins that are not loaded never show up.

### Outfit assignments

Livery dresses a character without changing their outfit record. **Assign** on the Pieces page dresses the character in the ticked pieces, replacing an outfit Livery gave them before; load a saved outfit first to dress them in it. **Release** ends it. F8, outside the window, releases everyone Livery dresses: do this before you uninstall Livery. F8 comes before the capture hotkey, so it never opens the capture window.

For each piece, Livery uses a copy the character already carries: the one they wear, else an enchanted one, else the most tempered one, else a plain one, so enchantments and tempering carry over. An NPC who carries none gets a new plain copy. You get one only when `AddMissingPieces` is on, because that creates items; otherwise the message names the pieces you do not carry. Once a piece is on, worn armor outside the outfit comes off, except non-playable armor and pieces another mod locked. When no piece can be put on, the character keeps what they wear.

An NPC keeps the pieces on: each is locked as with the game's PreventRemoval, so the NPC no longer swaps it for better armor you give them. The lock is saved with the item. When another mod takes a piece off, Livery puts it back on the next time the NPC's 3D loads, as long as they still carry it. The copies Livery added do not show in the trade menu, as an NPC's own outfit does not; pieces that were theirs or that you gave them do, and a piece you take back there stays with you. Your own pieces are never locked, since a locked piece cannot be taken off in the inventory.

Releasing a character removes the locks, takes back the copies Livery added, and puts back on their own pieces that Assign took off. Pieces that were theirs, or that you gave them, stay, worn or not, so a character dressed in gear they already owned keeps it on. A slot whose own piece is gone is filled when the game next re-evaluates the NPC, which can take a cell change or two. When an NPC dies, Livery removes the locks and forgets them; the copies stay on the corpse and nothing is put back on.

Assignments live in the SKSE co-save, so each save has its own. Livery adds no ESP, so uninstalling it cannot break a save: the locks and copies left on characters are ordinary item data. Releasing everyone first removes those too, except on NPCs out of reach (not persistent, and in a cell that is not loaded).

### Situations

**Assign for** on the Pieces page gives an NPC an outfit for a situation: **Town** (a city, town, settlement, inn or other inhabited interior), **Home** (a house), **Sleep** (going to bed, in it or getting up), **Swimming** (in water) or **Warm** (outdoors in cloudy, rainy or snowy weather, or anywhere outdoors in a snowy region). Load a saved outfit first: a situation outfit is taken from the library each time it goes on, so updating it there changes what they wear next time. The outfit assigned for **Default** is worn where no situation applies; a fight is always fought in it, a home without a Home outfit uses the Town one. An NPC with situation outfits but no Default wears their own clothes outside them.

Livery checks every second and redresses the NPC when their situation changes, one NPC per second when several change at once; nothing changes while the capture window is open. The line under the name shows what they wear now and the outfits chosen. The Situations page drops the outfit of one situation or several; Release clears everything, situations included. The player has no situations yet.

### Settings

Settings live in `Data\SKSE\Plugins\Livery.ini`, which the plugin creates with defaults on first start (keys a newer version adds are appended, so an update never resets your settings). They are read once when the game starts. With SKSE Menu Framework installed, the same settings are on its **Livery** page and take effect at once.

| Section   | Key                            | Default  | Meaning                                                                                               |
| --------- | ------------------------------ | -------- | ----------------------------------------------------------------------------------------------------- |
| `General` | `RestrictToFollowersAndSpouse` | `1`      | `1` captures only followers and your spouse; `0` captures any adult NPC.                              |
| `General` | `LivePreview`                  | `1`      | `1` opens the window with Live Preview on; `0` opens it with Live Preview off and the game paused.    |
| `UI`      | `Skin`                         | `Tailor` | Look of the capture window: `Tailor`, `Vanilla` or `Nordic`.                                          |
| `Hotkey`  | `Key`                          | `68`     | Capture key as a DirectInput scan code (`68` = F10). In game, click the hotkey button and press a key. |
| `Hotkey`  | `Modifier`                     | `0`      | Key to hold together with `Key`: `0` none, `42` Shift, `29` Ctrl, `56` Alt (left or right).           |
| `Camera`  | `SpeedPercent`                 | `100`    | Speed of the preview camera's movement, drags and wheel zoom, in percent (`10` to `200`).             |
| `Player`  | `AddMissingPieces`             | `0`      | `1` lets an outfit you give yourself add the pieces you do not carry, which creates items.            |

The outfit library is `Data\SKSE\Plugins\Livery\outfits.json`, shared by every save and character; each piece is stored as its plugin name and FormID, so an outfit survives a load order change. Back it up with your saves.

Skins: **Tailor** matches [Tailor](https://github.com/heathbrownkeyworks/Tailor) 3.0's menus and uses the fonts Tailor ships (ImGui's default font without Tailor); **Vanilla** mimics the game's menus; **Nordic** mimics NORDIC UI, with its corner knot and heading rope. The fonts and textures of the Vanilla and Nordic skins ship in `Data\SKSE\Plugins\Livery\` with their licences.

### Limits

- Only ASCII can be typed into the name field (the game's text input carries the ANSI code page).
- The window is drawn with Dear ImGui on Skyrim SE/AE only. On Skyrim VR the hotkey saves the outfit at once under the automatic name, with the detected armor class and gender and no situation.
- Pieces from plugins that are not loaded have no row in the window, and Update Existing leaves them in the outfit. Edit `outfits.json` to remove them.
- The window has no delete or rename. Edit `outfits.json` for those, with the game closed.
- Earlier versions, as a Tailor add-on, kept a working outfit named `Preview Outfit (used by Capture Outfit add-on)` in Tailor's library. It is no longer used; delete it in Tailor.
- Slots an assigned outfit leaves empty stay the game's: an NPC can put on a helmet or shield they own there.
- Situation outfits are for NPCs only, and the Default outfit cannot be dropped on its own: Release drops it with the situations.
- On Skyrim VR and on AE before 1.6.1130 the copies Livery adds to an NPC show in the trade menu, where you can take them for free; so do those of an NPC who has no default outfit.

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

The `deploy` target copies `livery.dll`, its PDB and the `Data/SKSE/Plugins/Livery` folder (skin fonts and textures) into `<deploy dir>/SKSE/Plugins/`. No INI is deployed; the plugin creates it on first start.

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

Copyright (c) 2026 Livery Contributors. Licensed under the GNU GPL v3.0 or later; see [LICENSE](LICENSE). CommonLibSSE-NG is also GPL-3.0-or-later. The SKSE Menu Framework header in `extern/` is LGPL-2.1; the Barlow Condensed and Sovngarde fonts are under the SIL Open Font License; the Nordic skin's ornaments are NORDIC UI art by Dopesan, used under the permissions on its mod page.
