# Livery — Claude Code Rules

SKSE plugin for Skyrim SE/AE (target runtime 1.6.1170), CommonLibSSE-NG v9.1.0 (`lib/commonlibsse-ng`), MSVC (VS 2026) + CMake presets + vcpkg. Livery dresses NPCs and the player in outfits of its own (`docs/design.md`): the capture hotkey (INI, default F10) on a crosshair NPC, or on the player when nothing is aimed at, opens an ImGui window that saves the worn armor into Livery's library (`Data/SKSE/Plugins/Livery/outfits.json`, `src/Library.cpp`) and assigns or releases an outfit by direct equip + PreventRemoval, kept in the SKSE co-save (`src/Assignments.cpp`, `src/Dressing.cpp`). Release everyone (the uninstall step) is a button on the SKSE Menu Framework settings page; no key does it.

Build (Windows): run inside the VS developer environment (`.vscode/tasks.json` does this; default build task = `Ctrl+Shift+B`):
`cmake --preset release-windows` then `cmake --build --preset release-windows` → `build/msvc/livery.dll`.

## 0. Output discipline

- Chat: **Japanese**. Docs, code comments, commit messages: **English**. Player-visible strings (HUD messages): keep as written; never translate or reword unless asked.
- **Player-facing English** (settings labels, tooltips, HUD messages, window text): write like a well-made Skyrim mod's MCM. Labels: 2–5 words, no trailing explanation; the detail goes in a tooltip of one or two sentences. Use the game's own vocabulary — items, inventory, equipped, favorites, followers, spouse, outfit — never Livery's internal terms (pieces, capture, captured actor, row, assignment) unless the UI already shows them. Address the player as "you". State the Off behavior only when it is not the obvious opposite. Prefer the existing in-repo wording only when the user asks for consistency; otherwise propose the shorter form and flag the old one. Term map: pieces → items; capture window → Livery / the window; captured NPC → the NPC; capture hotkey → hotkey.
- Docs are written for AI readers — concise, current spec only. No records of dropped/withdrawn ideas unless asked; a revivable idea goes to `docs/memo-*.md`.
- Never carry chat labels ("案A", "Model 1") into docs or comments; write the chosen design itself.
- No filler narration — outcomes, decisions, user input only. Never announce intent or post empty status.
- Subagents: few lines (what changed, decisions needed). No per-file lists, brief-restating, or self-congratulation.
- **Never report unverified behavior as confirmed.** Keep three states apart: built / verified by a tool (build, `dumpbin`, tests) / needs an in-game check.
- `AskUserQuestion`: when the deciding facts run over 3 lines, write them in a normal chat message first; the modal holds only a short question and brief option labels. Restate the decision and reason in the final message.
- **Doc review** (`docs/*.md`, `README.md` + 「検討」「レビュー」/review): each name, new term, API signature — grep collisions; idiomatic English. Unsuitable → give alternative. Material issues only; none → 「なし」. Code → §8.

## 1. Pre-change confirmation (required)

Before Edit/Write, get approval for: (1) paths, (2) per file what/why/behavior, (3) high-impact — prominent, labeled 「影響が大きい」, (4) new numeric constant placement + why (§3). Ask this approval in a plain chat message and end the turn — never via `AskUserQuestion` — so the user can switch model before implementation. Until approved: Read/Grep only. On send-back, re-present.

**A request ≠ approval.** Still confirm for: 「実装してください」「進めて」「お願いします」; settled/agreed approach; 1–2 files; 「ついでに」 follow-ons; tiny fix around a commit; contract comments / default-OFF debug logging only.

**Exempt (since 2026-10-21):** plain wording changes and small position / layout tweaks may be implemented without approval when nothing is in doubt. Any open question, an error in the request (typo, wrong name, mismatch with code or docs), or a possible breakage (layout, behavior) still needs approval first. High-impact items are never exempt.

**High impact:** hooks or memory patches (`REL::Relocation`, trampoline `write_call`/`write_branch`, vtable writes); per-frame or per-input logic (input / crosshair / menu sinks, update hooks); any new write to the outfit library (§2.1); SKSE serialization / co-save; SKSE message handling and init order (`kPostLoad`, `kDataLoaded`, `kPostLoadGame`, …); build system (`CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `cmake/`, submodule bumps); cross-cutting API change.

**Model hint:** at the top of every approval request and every §8 review, print one line `⚖️ モデル目安: 重い / 軽い — 理由` ("重い" = any high-impact item, 3+ files, or a new invariant), so the user can switch model before implementation. Also print it the moment a conversation turns to a high-impact topic.

**Speak up first** on ambiguous scope, unstated assumptions, or easy bugs / hard debt — surface the tradeoff (one question or alternative); never guess silently. Skip taste / easy reverts.

## 2. Where you may work

- **Edit:** `src/`, `test/`; build config per §1 high impact.
- **Docs:** update `README.md` (its Nexus section included) and `docs/` (`docs/nexus-page.md` included) when a code change makes them stale. `scripts/generate-nexus-page.py` may be changed as needed too (the rest of `scripts/` stays ask-first).
- **Never edit:** `lib/commonlibsse-ng`, `lib/vcpkg` (submodules; bump only via `scripts/update.sh` on request); `build/`; `docs/archive/` (the user's raw drafts, kept for people: do not read them either; `.claude/settings.json` denies Read there).
- **Ask first:** `.github/`, `scripts/`, `lefthook.yml`, lint configs (`.clang-format`, `.clang-tidy`, `.vale*`, `dprint.json`).
- **Never touch outside the repo:** the Skyrim install, MO2 mods / profiles / overwrite, other mods' data files — give the user steps instead. Deploy only to `build/msvc/deploy/` unless the user set `DEPLOY_DIR_OVERRIDE`.
- `lib/commonlibsse-ng/CLAUDE.md` and `AGENTS.md` describe upstream library development; they do not apply here. Read the rest of `lib/` only as reference.
- `lib/sarrai-common` is the user's own library (overlay hooks, window skins, free camera, skin fonts and textures), shared with Tableau. Change it in its own repo (`../sarrai-common`, whose `CLAUDE.md` applies), then pull it into the submodule here and commit the bump on a branch. A fresh clone of it needs `git -c protocol.file.allow=always submodule update --init lib/sarrai-common`.

### 2.1 Outfit library safety

The library (`outfits.json`, `src/Library.cpp`) is shared by every save and character: every write is permanent for the user.

- New outfit: an unused name (`Library::FindByName` first), at least one piece. Validate before `Library::Create` so nothing is written for a refused request.
- Change an existing outfit only through the Save page's Update tab, the Apply popup's Yes, or the Wardrobe page's Update popup or its Apply popup with unsaved edits, after the user confirmed it: `Library::Update` replaces its pieces wholesale, and its name as the user left it; a new name must be unused by any other outfit (ASCII case ignored); pieces from plugins that are not loaded stay in it. the Wardrobe page redresses them, or with its Apply popup dresses them, only after the write took.
- Remove an outfit only through the Wardrobe page's Remove popup, after the user saw who has it and confirmed: `Library::Remove` writes first, and a refused or failed write changes no assignment; then `Assignments::ForgetOutfit` takes it off every row (a Default this leaves wearing nothing keeps its pieces, read before the remove, as pieces not in the wardrobe), and those wearing it for a situation are redressed: the captured actor at once, the others at the window's close or, out of reach, when they load.
- Assign a saved outfit to the actor (Default or a situation) only as the Default by an Apply the user pressed (the Save page's Create and Apply included), for the situation the actor wears another outfit for now by Yes on the Wardrobe page's or Create and Apply's Apply popup, or as the Situations page's Save Changes saves the rows the user dragged outfits onto; replace the outfit a row wears only after the user pressed Confirm or Yes on the popup that names it, or dropped another one on that row and pressed Save Changes — never by default. The library write comes first; a refused or failed write assigns nothing.
- The items an outfit leaves off on a row (`OutfitRows::Exclusion`, co-save record `EXCL`) change only on those paths: the Wardrobe page's Apply with its ticks, the Situations page's Save Changes with the ticks of an outfit's details opened from the row, the Dress page's Apply when its ticks are all or some of the library outfit the actor wears and nothing else (it leaves the unticked ones off on the row that outfit is worn for); the Dress page's other Applies and Create and Apply give every item. The library outfit never changes for them.
- Never write a file that could not be read or that a newer version wrote (`Library::Load` sets the writable state once; an empty file, nothing but whitespace, is read as no file); write through a temporary file and a rename, so a failed write leaves the old file.
- `situations.json` (`src/SituationStore.cpp`) is shared by every save too: a situation's name or description changes only through the Situations page's Save Changes, a situation is removed only through the popup of the Situations page's Remove and restored only through Restore Presets' popup; every write is made before any assignment changes, and a refused or failed one changes none; the same load and write rules as the library (`SituationStore::Load` sets the writable state once).
- `chests.json` (`src/Chests.cpp`, docs/chest-ui.md) is shared by every save too: a chest is created only by the New chest popup's Create (empty, with an unused name), its name and items change only through the chest editor's Update or the unsaved-changes alert's Save Changes after the user pressed it, and a chest is removed only through the popup of the editor's Remove; every write comes first and a refused or failed one changes nothing; the same load and write rules as the library (`Chests::Load` sets the writable state once). A chest is never assigned or worn, so no chest write touches a row. An empty chest is allowed; a piece list of a chest may hold several items on one body slot.
- The live preview creates and writes nothing: it dresses through the inventory only.
- Library writes and functions taking `RE::Actor*`: game thread only (SKSE task).

### 2.2 Engine integration and API facts

- Prefer SKSE events / sinks over hooks. A hook needs approval (§1) and must use Address Library IDs via CommonLibSSE-NG (`REL::RelocationID`); never hard-code addresses or guess offsets / IDs.
- **Never guess an API's behavior.** Sources of truth: CommonLibSSE-NG headers and sources in `lib/`; `lib/sarrai-common` for the overlay, skins and free camera; `docs/design.md` for Livery's own contracts and the facts verified in game. Unspecified → say so, then verify or ask.

## 3. Development policy

- Stability and performance first; the simplest design that works.
- Normal play (the window closed, no preview) costs as close to nothing as possible: what runs then (the situation poll, event sinks, hooks) returns early before any allocation, lookup or logging, and does heavy work (a redress, a library scan) only when something changed.
- Small isolated changes — minimum code that solves the problem, nothing speculative; touch only what the task requires, clean up only your own mess (§4/§5 comment duties are not "extra"). No large rewrites unless asked; explain why before a large change to existing code. Readability refactors ≠ behavior changes in one change.
- State success criteria before implementing; verify what you can yourself (build, exports, unit tests). When only an in-game check can confirm, hand the user one concrete check (log line, HUD message, `outfits.json`, window state) instead of looping.
- Null-check what the engine or API may return null. No speculative try/catch, retries or fallbacks — add only after a reported failure.
- **Constants:** used in one function → local `constexpr`; shared in a file → anonymous-namespace `constexpr` at file top (`kName`); user-tunable → ask where it lives (INI etc.). Check single-use before hoisting. User says "constant" → follow.
- **Style:** `.clang-format` (CI checks `src/` and `test/`). Format only what you change.
- **Git:** commit each verified change on a branch (not `main`); Conventional Commits `feat|fix|chore|refactor` (defined in `lefthook.yml`; lefthook is not installed on this PC, so follow it by hand), English. Never push unless asked — `upstream` is the template repo; there is no `origin` yet.

## 4. Readability

Few-line fixes exempt.

- File order: constants → helpers → classes / event sinks → message handler → `SKSEPluginLoad`. `// ---` banners between roles.
- Functions ~40 lines; verb names; comment why and roles, not what. Units once.
- FILE MAP comment at the top at ~100+ lines or several roles.
- One file per feature by default; consult before splitting or appending to a file that grows too large or multi-responsibility.

## 5. Contract comments

When code relies on a non-obvious invariant — thread (game thread only), lifetime (handle vs raw pointer, inventory pointers stale after a native call), call order (the library is loaded at `kDataLoaded`), API limits (no delete in the window) — put a 1–3 line contract right above the spot. Code side mandatory.

```cpp
// Contract: runs on the game thread (SKSE task). Library writes are game-thread only and the
// window cannot delete an outfit, so every refusal must be ruled out before Library::Create.
```

## 6. Web research (recurring problems)

Same category fails code-only **twice+**: repo (`lib/commonlibsse-ng`, `docs/design.md`) then web. Steps: (1) organize symptom, repro, and fixes already tried; (2) JP+EN search; (3) prefer CommonLibSSE-NG repo / issues, Tailor repo docs, SKSE sources, open-source SKSE plugins, Stack Overflow; (4) present "why" before implement, cite URL. Never base an address, offset or API behavior on an unsourced forum post.

## 7. Session handoff

Only when asked (引き継ぎ / handoff / session-end). Never auto. Read `.claude/handoff-template.md` and follow it. Chat Markdown only; no Write to docs/issues unless asked. After long work, recommend a **new chat** (`/clear`). Request = Read/Grep only.

## 8. Code review

Report in **Japanese** plain text, never JSON: 要約 / 良い点 / 🔴🟡🟢 findings (`番号. 深刻度 — file:line — 一文要約`, next line 再現/影響) / 確認したいこと / テスト手順. If a tool such as `ReportFindings` is required, every field is Japanese.

- **🔴 always:** edits to `lib/` submodules (`lib/sarrai-common` only through its own repo); library writes or actor calls off the game thread; a write that can change or remove an existing outfit outside the confirmed update paths, or assign it to a row the user did not tick; a path that can write an empty outfit or an unreadable file; a close path that leaves the preview on, or a preview path that writes the library; hard-coded addresses / offsets; a hook added without approval.
- **🟡:** allocation, library scans or info-level logging on per-frame / per-input paths; raw `RE::` pointers kept across frames (use handles) or inventory pointers kept across a native call; unchecked engine singletons; changes to message-handler order.
- **Test steps to require:** build passes and `dumpbin /exports` shows `SKSEPlugin_Load` / `SKSEPlugin_Query` / `SKSEPlugin_Version`; in game — log shows `Livery loaded` and `Loaded N outfit(s) from outfits.json`; the capture hotkey on an adult NPC → HUD result, the outfit on the window's Load Outfit page; loading a save, and New Game only when the change touches what a new game runs differently (kNewGame / init order, co-save revert, the situation poll's start).
