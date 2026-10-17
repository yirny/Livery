# Memo: emptying the preview outfit on close

Not implemented. The preview outfit (`kPreviewOutfit`) keeps the last previewed pieces after the window closes; its name tells players not to pick it in Tailor.

## Goal

Empty the preview outfit whenever the capture window closes, so Tailor's list never shows a stale copy of the last preview.

## Findings (Tailor 3.0, API 1.0)

- Emptying it in the same SKSE task as `ClearOutfitOverride` makes the NPC look naked for a moment on close. An A/B test confirmed it: no flash without the emptying.
- During that emptying Tailor redresses nobody (`RefitOutfits: ... queued 0 loaded NPC(s)` per removal). Its `ResetOutfit` (back to the NPC's own outfit) runs after the removals in the same frame, and its equipment audit reports the own outfit settled about 1 s later.
- `GetOutfit` stops naming the preview outfit right after `ClearOutfitOverride`, so it cannot tell when the NPC has actually changed back.
- Inference, not verified: editing the outfit changes the dynamic outfit form Tailor built from it (`Created dynamic outfit` in Tailor.log), which the NPC still has equipped, so the engine unequips those pieces at once.
- Unverified: whether a task an SKSE task adds runs in the same frame. Until known, do not wait by re-queuing a task from itself (a time-based wait could then hang the game).
- Unrelated and older: ticking pieces quickly during Live Preview can make the NPC invisible for a frame, as each change redresses them.

## If revived

- After the close, wait about 2 s on a separate thread, then queue one SKSE task that empties the outfit, skipped when a load is under way (`g_ready` false) or a preview has started again (`g_previewActor` set).
- Or wait for the NPC's outfit change through a Tailor listener (`AddListener`), at the cost of managing its registration.
