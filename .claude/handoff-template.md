# Session Handoff Note — Template

Output format when the Agent is asked for a handoff note. **Chat output only** — never write the note into this file, `docs/`, or issues unless explicitly asked.
Depth, sections, budget, and project checks are defined **only here**; `CLAUDE.md` §7 holds just the trigger and read-only policy.

---

## Depth (from the request; **Standard** if unspecified)

| Request                                          | Sections                                                    |
| ------------------------------------------------ | ----------------------------------------------------------- |
| **Short** (「短く」)                             | Purpose + Incomplete / Next Steps (≤3 bullets) only         |
| **Standard** (unspecified)                       | All Standard Sections below; omit any that don't apply      |
| **Detailed** (「詳しく」 / for the next session) | Standard + Rationale, confirmed repro steps, Docs Promotion |

## Output budget (all depths)

- One line per bullet; no prose paragraphs.
- Never restate file contents or details already in chat — reference a commit, path, or `docs/*.md` section instead.
- Facts from chat + git only; mark guesses as "unverified".

> **Repro-steps split**: `Please Check` = unverified steps the user should confirm. `Rationale` (Detailed) = confirmed background and repro steps. Don't mix.

---

## Standard Sections

```markdown
## Handoff Note

### Status

- Branch `feat/xxx` @ `<commit>` (YYYY-MM-DD) / uncommitted changes: yes or no
- Last build: OK / failed / not run (preset `release-windows`)

### Purpose

(What this session was trying to accomplish)

### Incomplete / Next Steps

- (Remaining tasks, most urgent first; name specific files/functions)

### Completed

- (Implemented / fixed items; state verification: built / tool-verified / in-game OK / unverified)

### Constraints / Things Not to Touch

- (e.g. TailorAPI.h untouched, no hooks added, Tailor data only added to)

### Related Files

- `src/Plugin.cpp` — (role in one line; only files touched or needed next, not everything read)

### Please Check (optional)

- (Unverified repro steps / items to check in-game)

### Rationale (Detailed only)

- (Why this design/approach; confirmed repro steps)

### Docs Promotion (when applicable)

- (Spec decided this session that belongs in `docs/*.md` or `README.md` — 1–3 line proposal)
```

---

## Project-Specific Checks (fold into the sections above when applicable)

- **Tailor data**: any new building call, or a path that could leave an empty outfit or touch existing outfits? → Constraints / Incomplete
- **High impact**: hooks, per-input / per-frame logic, SKSE message order, build system → Constraints
- **Deployment**: DLL copied to MO2 or not; which Tailor / SKSE / Address Library versions were used → Status / Please Check
- **Verification**: steps tried + result (built / tool-verified / in-game OK / unverified / could not reproduce) → Completed / Please Check

---

## Paste example for the next session

Don't wrap the note in a code fence when pasting (nested fences break). Plain text:
`Continuing from last time — handoff note below. This time: ○○` + the note as-is.
