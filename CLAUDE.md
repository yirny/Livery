# Livery — Claude Code Rules

SKSE plugin for Skyrim SE/AE (target runtime 1.6.1170), CommonLibSSE-NG v9.1.0 (`lib/commonlibsse-ng`), MSVC (VS 2026) + CMake presets + vcpkg. Livery dresses NPCs and the player in outfits of its own (`docs/design.md`): the capture hotkey (INI, default F10) on a crosshair NPC, or on the player when nothing is aimed at, opens an ImGui window that saves the worn armor into Livery's library (`Data/SKSE/Plugins/Livery/outfits.json`, `src/Library.cpp`) and assigns or releases an outfit by direct equip + PreventRemoval, kept in the SKSE co-save (`src/Assignments.cpp`, `src/Dressing.cpp`). F8 releases everyone.

Build (Windows): run inside the VS developer environment (`.vscode/tasks.json` does this; default build task = `Ctrl+Shift+B`):
`cmake --preset release-windows` then `cmake --build --preset release-windows` → `build/msvc/livery.dll`.

## 0. Output discipline

- Chat: **Japanese**. Docs, code comments, commit messages: **English**. Player-visible strings (HUD messages): keep as written; never translate or reword unless asked.
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

**Speak up first** on ambiguous scope, unstated assumptions, or easy bugs / hard debt — surface the tradeoff (one question or alternative); never guess silently. Skip taste / easy reverts.

## 2. Where you may work

- **Edit:** `src/`, `test/`; build config per §1 high impact.
- **Docs:** update `README.md` and `docs/` when a code change makes them stale. Exception: `docs/nexus-page.md` (and the Nexus section of `README.md`, `scripts/generate-nexus-page.py`) only when the user asks.
- **Never edit:** `lib/commonlibsse-ng`, `lib/vcpkg` (submodules; bump only via `scripts/update.sh` on request); `build/`; `docs/archive/` (the user's raw drafts, kept for people: do not read them either; `.claude/settings.json` denies Read there).
- **Ask first:** `.github/`, `scripts/`, `lefthook.yml`, lint configs (`.clang-format`, `.clang-tidy`, `.vale*`, `dprint.json`).
- **Never touch outside the repo:** the Skyrim install, MO2 mods / profiles / overwrite, other mods' data files — give the user steps instead. Deploy only to `build/msvc/deploy/` unless the user set `DEPLOY_DIR_OVERRIDE`.
- `lib/commonlibsse-ng/CLAUDE.md` and `AGENTS.md` describe upstream library development; they do not apply here. Read `lib/` only as reference.

### 2.1 Outfit library safety

The library (`outfits.json`, `src/Library.cpp`) is shared by every save and character: every write is permanent for the user.

- New outfit: an unused name (`Library::FindByName` first), at least one piece, the categories the user ticked. Validate before `Library::Create` so nothing is written for a refused request.
- Change an existing outfit only through the Save page's Update tab, the Apply popup's Yes, or Manage Wardrobe's Update popup or its Apply popup (Update and Apply), after the user confirmed it: `Library::Update` replaces its pieces and the categories the window offers wholesale, and its name and gender as the user left them; the stored categories the window does not offer stay; a new name must be unused by any other outfit (ASCII case ignored); pieces from plugins that are not loaded stay in it. Manage Wardrobe changes the actor's rows, redresses them, or with Update and Apply dresses them, only after the write took.
- Remove an outfit only through Manage Wardrobe' Remove popup, after the user saw who has it and confirmed: `Library::Remove` writes first, and a refused or failed write changes no assignment; then `Assignments::ForgetOutfit` takes it off every row (a Default this leaves wearing nothing keeps its pieces, read before the remove, as pieces not in the wardrobe), and those wearing it for a situation are redressed: the captured actor at once with Live Preview on, the others at the window's close or, out of reach, when they load.
- Assign a saved outfit to the actor (Default or a situation) only for the rows the user ticked (the Save page's `wears it for`, Manage Wardrobe' `Situations for #{Name}`) or as the Default by an Apply the user pressed, and replace the outfit a row wears only after the user pressed Confirm on the popup that names it — never by default. The library write comes first; a refused or failed write assigns nothing.
- Never write a file that could not be read or that a newer version wrote (`Library::Load` sets the writable state once); write through a temporary file and a rename, so a failed write leaves the old file.
- `situations.json` (`src/SituationStore.cpp`) is shared by every save too: a situation's name or description changes only through Manage Situations' Save Changes, a situation is removed only through Remove Situation's popup and restored only through Restore Presets' popup; every write is made before any assignment changes, and a refused or failed one changes none; the same load and write rules as the library (`SituationStore::Load` sets the writable state once).
- The live preview creates and writes nothing: it dresses through the inventory only.
- Library writes and functions taking `RE::Actor*`: game thread only (SKSE task).

### 2.2 Engine integration and API facts

- Prefer SKSE events / sinks over hooks. A hook needs approval (§1) and must use Address Library IDs via CommonLibSSE-NG (`REL::RelocationID`); never hard-code addresses or guess offsets / IDs.
- **Never guess an API's behavior.** Sources of truth: CommonLibSSE-NG headers and sources in `lib/`; `docs/design.md` for Livery's own contracts and the facts verified in game. Unspecified → say so, then verify or ask.

## 3. Development policy

- Stability and performance first; the simplest design that works.
- Small isolated changes — minimum code that solves the problem, nothing speculative; touch only what the task requires, clean up only your own mess (§4/§5 comment duties are not "extra"). No large rewrites unless asked; explain why before a large change to existing code. Readability refactors ≠ behavior changes in one change.
- State success criteria before implementing; verify what you can yourself (build, exports, unit tests). When only an in-game check can confirm, hand the user one concrete check (log line, HUD message, `outfits.json`, window state) instead of looping.
- Null-check what the engine or API may return null. No speculative try/catch, retries or fallbacks — add only after a reported failure.
- **Constants:** used in one function → local `constexpr`; shared in a file → anonymous-namespace `constexpr` at file top (`kName`); user-tunable → ask where it lives (INI etc.). Check single-use before hoisting. User says "constant" → follow.
- **Style:** `.clang-format` (CI checks `src/` and `test/`). Format only what you change.
- **Git:** commit each verified change on a branch (not `main`); Conventional Commits `feat|fix|chore|refactor` (enforced by `lefthook.yml`), English. Never push unless asked — `origin` is the template upstream.

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

- **🔴 always:** edits to `lib/` submodules; library writes or actor calls off the game thread; a write that can change or remove an existing outfit outside the confirmed update paths, or assign it to a row the user did not tick; a path that can write an empty outfit or an unreadable file; a close path that leaves the preview on, or a preview path that writes the library; hard-coded addresses / offsets; a hook added without approval.
- **🟡:** allocation, library scans or info-level logging on per-frame / per-input paths; raw `RE::` pointers kept across frames (use handles) or inventory pointers kept across a native call; unchecked engine singletons; changes to message-handler order.
- **Test steps to require:** build passes and `dumpbin /exports` shows `SKSEPlugin_Load` / `SKSEPlugin_Query` / `SKSEPlugin_Version`; in game — log shows `Livery loaded` and `Loaded N outfit(s) from outfits.json`; the capture hotkey on an adult NPC → HUD result, the outfit on the window's Load Outfit page; loading a save, and New Game only when the change touches what a new game runs differently (kNewGame / init order, co-save revert, the situation poll's start).
