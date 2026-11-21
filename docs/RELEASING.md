# Releasing

## Versioning

The version lives in two places: `CMakeLists.txt` (`project(... VERSION x.y.z)`) and `vcpkg.json` (`version-string`). Bump both together.

## Publishing a release

1. Run `./scripts/release.sh <major|minor|patch|X.Y.Z>` on `main`. It bumps `CMakeLists.txt` and `vcpkg.json` on a `chore/release-X.Y.Z` branch, opens a PR (labeled `ignore-for-release`), waits for its checks, squash-merges it, then tags and pushes `vX.Y.Z`. That push triggers `release.yml` to build the plugin, package it via `scripts/package.sh`, and publish a GitHub Release with the zip and PDB. The `CMakeLists.txt` version is also embedded in the DLL through the `SKSEPlugin_Version` that `add_commonlibsse_plugin` generates, and `release.yml` names the zip after the tag. Keep all three (files, tag, DLL) in lockstep. Requires the GitHub CLI (`gh`), authenticated with push/merge access.
2. Upload the zip to Nexus Mods by hand on the mod's Manage Files page. Add the version's lines to `docs/nexus-changelog.txt` (the Nexus changelog as pasted, one line per entry, newest version first) and paste them into the file's changelog there. If the Nexus page changed, paste `docs/nexus-bbcode.bbcode` into the Nexus Mods page editor.

