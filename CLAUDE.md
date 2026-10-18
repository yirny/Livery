# Livery — Claude Code Rules

SKSE plugin for Skyrim SE/AE (target runtime 1.6.1170), CommonLibSSE-NG v9.1.0 (`lib/commonlibsse-ng`), MSVC (VS 2026) + CMake presets + vcpkg. Talks to Tailor through its mod API 1.0 (`src/TailorAPI.h`, Tailor 3.0+). Feature: the capture hotkey (INI; its default F8 is taken by Livery's release-everyone key for now) on a crosshair NPC opens an ImGui window (name + built-in categories) and saves the armor they wear as a new Tailor outfit. Livery's own outfit manager (`docs/design.md`, stage 1 in): F10 / F8 keys assign and release outfits by direct equip + PreventRemoval, kept in the SKSE co-save (`src/Assignments.cpp`, `src/Dressing.cpp`).

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

**High impact:** hooks or memory patches (`REL::Relocation`, trampoline `write_call`/`write_branch`, vtable writes); per-frame or per-input logic (input / crosshair / menu sinks, update hooks); any new call that changes Tailor data (§2.1); SKSE serialization / co-save; SKSE message handling and init order (`kPostLoad`, `kDataLoaded`, `kPostLoadGame`, …); build system (`CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `cmake/`, submodule bumps); cross-cutting API change.

**Speak up first** on ambiguous scope, unstated assumptions, or easy bugs / hard debt — surface the tradeoff (one question or alternative); never guess silently. Skip taste / easy reverts.

## 2. Where you may work

- **Edit:** `src/` (except `src/TailorAPI.h`), `test/`; build config per §1 high impact.
- **Docs:** update `README.md` and `docs/` when a code change makes them stale. Exception: `docs/nexus-page.md` (and the Nexus section of `README.md`, `scripts/generate-nexus-page.py`) only when the user asks.
- **Never edit:** `src/TailorAPI.h` — official header, must stay identical to upstream `docs/api/TailorAPI.h` (replace with a newer upstream copy only on request); `lib/commonlibsse-ng`, `lib/vcpkg` (submodules; bump only via `scripts/update.sh` on request); `build/`.
- **Ask first:** `.github/`, `scripts/`, `lefthook.yml`, lint configs (`.clang-format`, `.clang-tidy`, `.vale*`, `dprint.json`).
- **Never touch outside the repo:** the Skyrim install, MO2 mods / profiles / overwrite, Tailor's data files — give the user steps instead. Deploy only to `build/msvc/deploy/` unless the user set `DEPLOY_DIR_OVERRIDE`.
- `lib/commonlibsse-ng/CLAUDE.md` and `AGENTS.md` describe upstream library development; they do not apply here. Read `lib/` only as reference.

### 2.1 Tailor data safety

The Tailor library (outfits, categories) is shared by every save and character, and API 1.0 cannot delete outfits or categories: every building call is permanent for the user.

- New outfit: an unused name (`DoesOutfitExist` first), its pieces, then its category (pieces before category).
- Change an existing outfit's pieces only through Update Existing, after the user confirmed that outfit in the dialog: add the new pieces first, stop if none was accepted, then remove the old ones — never leave it empty. Never rename an existing outfit or change its gender.
- Add categories to an existing outfit only through Update Existing: the ones the user ticked in the dialog and confirmed, after its pieces were replaced. Categories it is already in are shown ticked and locked (API 1.0 cannot remove one); never tick a new one by default.
- Put outfits into situation pools (Adventuring, Town, Home, Sleep, Swimming, Warm) only when the user ticked them in the window — never by default.
- Validate inputs before `CreateOutfit` so a refusal cannot leave an empty outfit.
- Live preview exception: the working outfit `Preview Outfit (used by Capture Outfit add-on)` (`kPreviewOutfit`) is the only outfit created or changed before the user confirms. Create it unisex with no category; it may hold any pieces, empty included. Never put it in a category, never list it as an Update Existing target.
- Overrides: set one only on the captured actor, and only when `HasOutfitOverride` is false when the window opens — never replace another mod's override (API 1.0 cannot read it back). Every close without a save ends it (`ClearOutfitOverride`); a save moves it to the saved outfit.
- Building functions and functions taking `RE::Actor*`: game thread only (SKSE task).

### 2.2 Engine integration and API facts

- Prefer SKSE events / sinks and the Tailor API over hooks. A hook needs approval (§1) and must use Address Library IDs via CommonLibSSE-NG (`REL::RelocationID`); never hard-code addresses or guess offsets / IDs.
- **Never guess an API's behavior.** Sources of truth: `src/TailorAPI.h` comments + upstream `docs/api/README.md` (https://github.com/heathbrownkeyworks/Tailor/blob/main/docs/api/README.md); CommonLibSSE-NG headers and sources in `lib/`. Unspecified → say so, then verify or ask.

## 3. Development policy

- Stability and performance first; the simplest design that works.
- Small isolated changes — minimum code that solves the problem, nothing speculative; touch only what the task requires, clean up only your own mess (§4/§5 comment duties are not "extra"). No large rewrites unless asked; explain why before a large change to existing code. Readability refactors ≠ behavior changes in one change.
- State success criteria before implementing; verify what you can yourself (build, exports, unit tests). When only an in-game check can confirm, hand the user one concrete check (log line, HUD message, Tailor UI state) instead of looping.
- Null-check what the engine or API may return null. No speculative try/catch, retries or fallbacks — add only after a reported failure.
- **Constants:** used in one function → local `constexpr`; shared in a file → anonymous-namespace `constexpr` at file top (`kName`); user-tunable → ask where it lives (INI etc.). Check single-use before hoisting. User says "constant" → follow.
- **Style:** `.clang-format` (CI checks `src/` and `test/`). Format only what you change; never reformat `src/TailorAPI.h`.
- **Git:** commit each verified change on a branch (not `main`); Conventional Commits `feat|fix|chore|refactor` (enforced by `lefthook.yml`), English. Never push unless asked — `origin` is the template upstream.

## 4. Readability

Few-line fixes exempt.

- File order: constants → helpers → classes / event sinks → message handler → `SKSEPluginLoad`. `// ---` banners between roles.
- Functions ~40 lines; verb names; comment why and roles, not what. Units once.
- FILE MAP comment at the top at ~100+ lines or several roles.
- One file per feature by default; consult before splitting or appending to a file that grows too large or multi-responsibility.

## 5. Contract comments

When code relies on a non-obvious invariant — thread (game thread only), lifetime (handle vs raw pointer, Tailor callback strings valid only during the call), call order (Tailor library ready after `kDataLoaded`), API limits (no delete) — put a 1–3 line contract right above the spot. Code side mandatory.

```cpp
// Contract: runs on the game thread (SKSE task). Tailor building calls are game-thread only,
// and API 1.0 cannot delete an outfit, so every refusal must be ruled out before CreateOutfit.
```

## 6. Web research (recurring problems)

Same category fails code-only **twice+**: repo (`lib/commonlibsse-ng`, `src/TailorAPI.h`) then web. Steps: (1) organize symptom, repro, and fixes already tried; (2) JP+EN search; (3) prefer CommonLibSSE-NG repo / issues, Tailor repo docs, SKSE sources, open-source SKSE plugins, Stack Overflow; (4) present "why" before implement, cite URL. Never base an address, offset or API behavior on an unsourced forum post.

## 7. Session handoff

Only when asked (引き継ぎ / handoff / session-end). Never auto. Read `.claude/handoff-template.md` and follow it. Chat Markdown only; no Write to docs/issues unless asked. After long work, recommend a **new chat** (`/clear`). Request = Read/Grep only.

## 8. Code review

Report in **Japanese** plain text, never JSON: 要約 / 良い点 / 🔴🟡🟢 findings (`番号. 深刻度 — file:line — 一文要約`, next line 再現/影響) / 確認したいこと / テスト手順. If a tool such as `ReportFindings` is required, every field is Japanese.

- **🔴 always:** edits to `lib/` submodules or `src/TailorAPI.h`; Tailor building or actor calls off the game thread; a call that can change or remove an existing Tailor outfit / category outside the confirmed Update Existing path (or in an order that can empty it), or add to a situation pool or category unticked; a path that can leave an empty outfit (other than `Preview Outfit (used by Capture Outfit add-on)`); an override set over an existing one, or a close path that leaves the preview override on; hard-coded addresses / offsets; a hook added without approval.
- **🟡:** allocation, Tailor library scans or info-level logging on per-frame / per-input paths; raw `RE::` pointers kept across frames (use handles); Tailor callback strings kept after the callback returns; unchecked engine singletons; changes to message-handler order.
- **Test steps to require:** build passes and `dumpbin /exports` shows `SKSEPlugin_Load` / `SKSEPlugin_Query` / `SKSEPlugin_Version`; in game — log shows `Livery loaded` and `Tailor interface found`; the capture hotkey on an adult NPC → HUD result, outfit visible in Tailor; both New Game and loading a save.
