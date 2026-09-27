# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2717.1-run-gdb-quit-on-exit` (base `0de0646`).

### 2717.1 — biltoo-run-gdb quits on normal exit
- After `-ex run`, Python checks `$_exitcode`; `quit` only when status is 0.
- Crash / signal / non-zero exit → interactive prompt (bt still available).
- Also: `set debuginfod enabled off` (less banner noise).

### Prior (already on origin/master)
- Contact sheet + Strip rows (and follow-ups).
- OCR page-space / Source DPI / ContentXform overlays.
- TTS SpeakPlan, selection anchor, Pause/Resume, Messages dock.

### Later — crop-based packs (careful)
- **Re-enable Grid Crop** in UI: square cells, cover-scale + centre crop for *layout
  display only* — must **not** touch session/user content crop or durable appearance.
  Easy to confuse with content crop; keep a separate “layout clip” on the item
  (already `PackPose.cellSize` / gallery clip path) and never write crop into ItemWorld.
- Other crop schemes to consider (inspired by comic readers / contact sheets):
  - **Auto border trim** for overview only (CDisplayEx-style), not durable.
  - **Fixed-aspect cells** (1:1, 4:3) with letterbox vs cover+clip as a pack option.
- Do not invent more “Fill” modes until Contact sheet + Strip rows feel right in use.

### OCR / rotation (later)
- Tesseract OSD / per-line baseline for rotated overlays.

### Apply
```bash
git pull --ff-only …/biltoo-2717.1-run-gdb-quit-on-exit-0de0646.bundle HEAD
```
