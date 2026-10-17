# Tailor Outfit Capture

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that saves the armor an NPC, or you, are wearing as a new
[Tailor](https://github.com/heathbrownkeyworks/Tailor) outfit, or puts it on an outfit Tailor already has. Aim at the
NPC (or at nothing, for yourself), press the hotkey, name the outfit in a small window, done.

Built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), [CMake](https://cmake.org) and [vcpkg](https://vcpkg.io).

---

<!-- nexus:start -->

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Tailor](https://github.com/heathbrownkeyworks/Tailor) 3.0 or newer (mod API 1.0)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for SE and AE, or [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101) for VR
- Optional: SKSE Menu Framework, for changing the settings in game

## Installation

**Mod manager (recommended):**

1. Install the requirements above.
2. Install Tailor Outfit Capture via your mod manager.
3. Launch Skyrim via SKSE.

**Manual:**

1. Install the requirements above.
2. Copy the `SKSE` folder from the archive into `Data\` (it holds `tailoroutfitcapture.dll` and the fonts and textures of the window skins).
3. Launch Skyrim via SKSE.

## Compatibility

- Compatible with Skyrim SE, AE, and VR. On VR there is no capture window: the outfit is saved under an automatic name.
- No ESP/ESL required.

<!-- nexus:end -->

---

## What the Plugin Does

1. Load a save or start a new game.
2. Aim the crosshair at a follower or your spouse (any adult NPC once `RestrictToFollowersAndSpouse` is off; never a child). With nothing under the crosshair the hotkey captures the player instead.
3. Press **F8** (configurable, see [Settings](#settings)). The capture window opens and the character wears the ticked pieces (see [Live Preview](#live-preview)); with Live Preview off the game freezes instead. While the window is open the HUD is hidden and the game cannot be saved.

The window has four pages:

- **Pieces** — the heading shows the character's name and, below it, where Tailor judges them to be and what they wear, for example `In Town, wearing "Jarl's Finery" with changes.` Under it every armor piece the character wears or carries is listed with a checkbox: worn pieces start ticked except wigs (pieces on the Hair slot only), carried ones unticked. Ticking a piece unticks the ticked ones that share a body slot with it. **Load Outfit...** at the right end of the piece count opens the Load Outfit page; it is greyed while Tailor has no outfits. The **Live Preview** tick turns the preview on and off. **Create New** and **Update Existing** act on the ticked pieces, so both need at least one; **Cancel** or Esc closes the window and creates nothing. Throughout the window, a greyed button shows why on hover.
- **Load Outfit** — the outfits Tailor already has (left), with the selected one's categories and pieces (right). **Load** ticks exactly its pieces and returns, adding those the character does not carry to the list; it is greyed for an outfit none of whose pieces is loaded. **Back** returns with the ticks unchanged.
- **Name** (after Create New) — the outfit name, pre-filled with the first free `Captured Outfit NNN`; the categories; and the outfit's gender. Press Enter or **Create** to save, **Back** to return to the pieces. An empty name field says so inside the field; when a save fails, the field turns red and shows the reason on hover until you edit the name.
- **Update Existing** — the outfits Tailor already has (left), with the selected one's categories and pieces (right). **Update** asks for confirmation, then replaces the outfit's pieces with the ticked ones and adds it to the categories you ticked. Its name, gender and existing categories stay as they are. The confirmation says how many of its pieces come from plugins that are not loaded; those stay in it.

On both outfit pages, the piece count includes pieces from plugins that are not loaded and says how many those are; they have no row in the list. The selected outfit is shown on the live preview, and clicking it again deselects it. Deselecting it, or going **Back**, shows the ticked pieces again.

The search field (magnifying glass) and the filter dropdown (funnel) above the list narrow it, and the **Sort by** dropdown at the list's top right orders it. The search keeps the outfits whose name contains the typed text, ignoring case. The filter keeps those in the ticked categories: any ticked one within a group, and every group with a tick; a `No ...` row (for example `No Situation`) keeps the outfits in none of that group's categories. **Sort by** orders the list by Name, by Armor Type (then situation, then name) or by Situation (then armor type, then name), following Tailor's category order with outfits in none of them last. A field that narrows the list is underlined in the accent colour, and its cross clears it; Esc clears the search before it goes back. A selected outfit stays selected, and named on the right, while the list hides it.

The result is shown as a HUD message. Apart from the preview's working outfit (see [Live Preview](#live-preview)), the plugin creates nothing until you confirm, and it never deletes or renames anything: Tailor's mod API 1.0 cannot delete an outfit or take one out of a category, so the window warns before every step that cannot be undone.

### Live Preview

While **Live Preview** is ticked, the game keeps running and the character wears the ticked pieces, or the outfit selected on the Load Outfit or Update Existing page, as a Tailor outfit override, seen through a free camera. With it off the game is paused and the view stays as it was. The window opens with the preview on or off as `LivePreview` says (see [Settings](#settings)); the tick switches it at any time.

With the cursor over the game rather than the window:

| Input         | Camera                                                                  |
| ------------- | ----------------------------------------------------------------------- |
| W / A / S / D | Moves; hold Shift for half speed. Ignored while a text field is active. |
| Left drag     | Sideways turns the view around the character; up and down tilts it.     |
| Right drag    | Pans.                                                                   |
| Mouse wheel   | Zooms.                                                                  |

`SpeedPercent` scales all four.

The preview dresses the character in a working outfit named `Preview Outfit (used by Capture Outfit add-on)`, kept in Tailor's library with no category, so Tailor never picks it at random. It is the only outfit the plugin changes before you confirm. Every close of the window, a save included, ends the override, and the character goes back to what Tailor would dress them in otherwise.

Each time Tailor changes an NPC's outfit, the game removes every copy of the old outfit's pieces from their inventory, including copies that are their own, such as armor you gave them. The plugin notes an NPC's own armor as the window opens. About 2 seconds after the window closes, it adds back any copies that are gone and puts back on what they wore of their own.

### Categories and gender

Tailor's armor classes (Clothing, Light Armor, Heavy Armor) are checkboxes; its situations (Adventuring, Town, Home, Sleep, Swimming, Warm) and the custom categories you created in Tailor, read from its `library.json`, are dropdowns. On the Update Existing page, the categories the outfit is already in are ticked and greyed out, and say why on hover. The armor class is detected from the worn pieces (body first, then head, hands, feet, circlet) and ticked for you; no situation is ever ticked by default, so a captured outfit never enters a situation pool unless you put it there.

Gender is suggested the same way: **Female** when the deciding piece has only a female model, **Unisex** otherwise, and never Male unless you pick it. Tailor cannot change an outfit's gender later.

### What gets captured

Every armor piece from a plugin that the character wears or carries. Non-playable armor, such as the physics objects of body mods, is left out. Player-enchanted gear is saved as the plugin armor it was made from (Tailor refuses armor the game created at runtime); other runtime-created armor is left out. Pieces from plugins that are not loaded never show up.

### Settings

Settings live in `Data\SKSE\Plugins\TailorOutfitCapture.ini`, which the plugin creates with defaults on first start (keys a newer version adds are appended, so an update never resets your settings). They are read once when the game starts. With SKSE Menu Framework installed, the same settings are on its **Tailor Outfit Capture** page and take effect at once.

| Section   | Key                            | Default  | Meaning                                                                                              |
| --------- | ------------------------------ | -------- | ---------------------------------------------------------------------------------------------------- |
| `General` | `RestrictToFollowersAndSpouse` | `1`      | `1` captures only followers and your spouse; `0` captures any adult NPC.                             |
| `General` | `LivePreview`                  | `1`      | `1` opens the window with Live Preview on; `0` opens it with Live Preview off and the game paused.   |
| `UI`      | `Skin`                         | `Tailor` | Look of the capture window: `Tailor`, `Vanilla` or `Nordic`.                                         |
| `Hotkey`  | `Key`                          | `66`     | Capture key as a DirectInput scan code (`66` = F8). In game, click the hotkey button and press a key. |
| `Hotkey`  | `Modifier`                     | `0`      | Key to hold together with `Key`: `0` none, `42` Shift, `29` Ctrl, `56` Alt (left or right).          |
| `Camera`  | `SpeedPercent`                 | `100`    | Speed of the preview camera's movement, drags and wheel zoom, in percent (`10` to `200`).            |

Skins: **Tailor** matches Tailor 3.0's own menus and uses the fonts Tailor ships; **Vanilla** mimics the game's menus; **Nordic** mimics NORDIC UI, with its corner knot and heading rope. The fonts and textures of the Vanilla and Nordic skins ship in `Data\SKSE\Plugins\TailorOutfitCapture\` with their licences.

### Limits

- Only ASCII can be typed into the name field (the game's text input carries the ANSI code page).
- The window is drawn with Dear ImGui on Skyrim SE/AE only. On Skyrim VR the hotkey saves the outfit at once under the automatic name, with the detected armor class and gender and no situation.
- Outfit names on the Update Existing page come from Tailor's `outfits.json`, checked against Tailor's library; an outfit Tailor has not written to disk yet is not listed. The same holds for the Load Outfit page.
- Pieces from plugins that are not loaded cannot be listed or removed through Tailor's API 1.0, so Update Existing leaves them in the outfit. Remove them in Tailor with those plugins loaded.
- Live Preview is unavailable for a character another mod has set an outfit override on: the tick is greyed and its tooltip says why. The plugin never replaces another mod's override, because API 1.0 cannot read it back.
- On the player, unticking the last ticked piece leaves the preview still wearing it: Tailor does not dress the player in an empty outfit. NPCs are undressed as expected, and the saved outfit follows the ticks either way.
- An NPC's own armor that the plugin gives back comes back plain: tempering and a custom name are lost, because the plugin cannot create that item data.
- `Preview Outfit (used by Capture Outfit add-on)` stays in Tailor's library once the preview has run, because API 1.0 cannot delete an outfit. The Load Outfit and Update Existing pages leave it out. If you delete it in Tailor, the next preview creates it again.

Details go to `Documents\My Games\Skyrim Special Edition\SKSE\TailorOutfitCapture.log` (`Skyrim Special Edition GOG` for the GOG version).

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

The DLL lands in `build/msvc/tailoroutfitcapture.dll`.

In VS Code, `.vscode/tasks.json` runs the same commands inside the VS developer environment; the default build task (`Ctrl+Shift+B`) builds and deploys in one go.

#### Linux (clang-cl + xwin)

```bash
./scripts/build.sh
# or directly:
cmake --preset release-linux
cmake --build --preset release-linux
```

The DLL lands in `build/release-linux/tailoroutfitcapture.dll`.

#### Deploy to a mod manager

The `deploy` target copies `tailoroutfitcapture.dll`, its PDB and the `Data/SKSE/Plugins/TailorOutfitCapture` folder (skin fonts and textures) into `<deploy dir>/SKSE/Plugins/`. No INI is deployed; the plugin creates it on first start.

The deploy directory is, in order: `-DDEPLOY_DIR_OVERRIDE=<path>` given at configure time; `$SKYRIM_MODS_FOLDER/tailoroutfitcapture` (your mod manager's staging folder); `$SKYRIM_FOLDER/Data`; else `build/<preset>/deploy/`. Copy `.env.example` to `.env` to set the environment variables.

```bash
# Windows: pin the destination once, then build the deploy target
cmake --preset release-windows -DDEPLOY_DIR_OVERRIDE="C:/path/to/MO2/mods/Tailor Outfit Capture"
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

### Tailor API header

`src/TailorAPI.h` is Tailor's official header, kept identical to the upstream copy in Tailor's `docs/api/`. Replace it with a newer upstream copy instead of editing it.

---

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

Copyright (c) 2026 Tailor Outfit Capture Contributors. Licensed under the GNU GPL v3.0 or later; see [LICENSE](LICENSE). CommonLibSSE-NG is also GPL-3.0-or-later. The SKSE Menu Framework header in `extern/` is LGPL-2.1; the Barlow Condensed and Sovngarde fonts are under the SIL Open Font License; the Nordic skin's ornaments are NORDIC UI art by Dopesan, used under the permissions on its mod page.
