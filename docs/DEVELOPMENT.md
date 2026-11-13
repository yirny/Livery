# Development

Built with [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), [CMake](https://cmake.org) and [vcpkg](https://vcpkg.io). Releasing is in [RELEASING.md](RELEASING.md).

## Prerequisites

- [Git](https://git-scm.com/) and [CMake](https://cmake.org/download/) 3.21+.
- The `vcpkg` and `CommonLibSSE-NG` submodules: clone with `--recurse-submodules`, or run `git submodule update --init --recursive`.
- Windows: [Visual Studio 2022 or newer](https://visualstudio.microsoft.com/) (or the standalone Build Tools) with **Desktop development with C++**, which provides MSVC, CMake and Ninja.
- Linux (cross-compilation, as CI does): LLVM/Clang (`clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc`, `llvm-mt`), [Ninja](https://ninja-build.org/) and [xwin](https://github.com/Jake-Shadle/xwin) with the Windows SDK and MSVC CRT fetched once by `xwin splat --output ~/.xwin`. The first configure creates TitleCase symlinks inside `~/.xwin` (lld-link is case-sensitive); the originals are untouched.

## Build

Windows, from a **Developer Command Prompt for VS** (or after `vcvars64.bat`), so `cl.exe` and the Windows SDK are on `PATH`:

```bash
cmake --preset release-windows
cmake --build --preset release-windows
```

The DLL lands in `build/msvc/livery.dll`. In VS Code, `.vscode/tasks.json` runs the same commands inside the VS developer environment; the default build task (`Ctrl+Shift+B`) builds and deploys.

Linux: `./scripts/build.sh`, or the `release-linux` preset; the DLL lands in `build/release-linux/livery.dll`.

## Deploy

The `deploy` target copies `livery.dll`, its PDB and the `Data/SKSE/Plugins/Livery` folder (skin fonts and textures) into `<deploy dir>/SKSE/Plugins/`. No INI or palette is deployed; the plugin creates them on first start.

The deploy directory is, in order: `-DDEPLOY_DIR_OVERRIDE=<path>` given at configure time; `$SKYRIM_MODS_FOLDER/livery`; `$SKYRIM_FOLDER/Data`; else `build/<preset>/deploy/`. Copy `.env.example` to `.env` to set the environment variables.

```bash
# Windows: pin the destination once, then build the deploy target
cmake --preset release-windows -DDEPLOY_DIR_OVERRIDE="C:/path/to/MO2/mods/Livery"
cmake --build --preset release-windows --target deploy

# Linux (deploy.sh sources .env)
./scripts/deploy.sh
```

## Tests

Unit tests in `test/` use [Catch2](https://github.com/catchorg/Catch2) and build without CommonLibSSE, so only pure-logic code (no `RE::` / `SKSE::` APIs) can be tested. `test-windows` takes Catch2 from vcpkg; `test-linux` uses the system Catch2 3 package.

```bash
cmake --preset test-windows
cmake --build --preset test-windows
ctest --preset test-windows
```

## Git hooks

`lefthook.yml` defines a commit-msg check (Conventional Commits: `feat`, `fix`, `chore`, `refactor`) and pre-commit formatting and lint checks. To enable them, install [Lefthook](https://github.com/evilmartians/lefthook) and run `lefthook install`; the checks need `clang-format`, `clang-tidy`, `cmake-format`, `shellcheck`, `dprint` and `vale`.

## Updating CommonLibSSE-NG

The script pins the submodule to a release tag (`v*`) on the `ng` branch and stages it; without an argument it picks the latest tag.

```bash
./scripts/update.sh          # latest release tag
./scripts/update.sh v10.1.0  # a specific tag
git commit -m "chore(deps): update CommonLibSSE-NG to v10.1.0"
```

## Nexus Mods page

`docs/nexus-bbcode.bbcode` is the Nexus Mods page as pasted into its editor, maintained by hand. When a change makes it stale, edit it and paste the whole file into the Nexus Mods page editor; keep its Requirements, Installation and Compatibility in step with the `<!-- nexus:start/end -->` block in `README.md`.

## CI

| Workflow           | Trigger                                                                                               | What it does                                                                                                                              |
| ------------------ | ----------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `ci.yml`           | PRs to `main` touching `src/`, `test/`, `cmake/`, `vcpkg.json`, `CMakeLists.txt`, `CMakePresets.json` | `clang-format` (ubuntu) → `test-linux` and `build-linux` (ubuntu) and `test-windows` + `build-windows` + `clang-tidy` (windows, parallel) |
| `release.yml`      | Push of a `v*` tag                                                                                    | Builds, packages via `scripts/package.sh`, publishes a GitHub Release with zip + PDB                                                      |
| `nexus-upload.yml` | Auto-triggered via `workflow_run` once `release.yml` completes, or manual `workflow_dispatch`         | Downloads release zip, generates cliff release notes, uploads to Nexus Mods                                                               |
| `lint.yml`         | PRs touching `scripts/`, `README.md`, or `docs/`                                                      | Runs shellcheck on shell scripts, plus dprint formatting and Vale prose checks on Markdown                                                |
| `pr-title.yml`     | PR opened/edited/reopened/synchronize                                                                 | Checks PR title follows Conventional Commits (`feat`, `fix`, `chore`, `refactor`)                                                         |

`setup.yml` is left over from the repository template; it only runs in a repo newly created from a template. `nexus-upload.yml` waits for approval in the `nexus` environment; its one-time setup is in [RELEASING.md](RELEASING.md#nexus-mods-upload).
