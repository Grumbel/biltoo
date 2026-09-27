# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2718.2-world-vs-viewpoint` (base `0de0646`).

Stack (single line from origin/master):

1. **2717.1** — biltoo-run-gdb quits on normal exit (`$_exitcode == 0`); debuginfod off.
2. **2718.2** — World vs viewpoint (docs): session/speech/activity outlive canvas mode.

### World vs viewpoint (docs)
- Normative section in [DOMAIN.md](DOMAIN.md#world-vs-viewpoint).
- Cross-links: TEXT_TO_SPEECH, MODE_OWNERSHIP, ACTIVITY, AGENTS.
- Philosophy, not a feature checklist — call out violations in review.

### Open product direction (not scheduled)
- TTS continues across mode switches / page flips; speak plan may advance
  across session images; Gallery highlights speech-cursor page.
- Surfaces report background tile/soft/OCR activity without owning it.
- Longer term: multiple documents in one process under the same rule.

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
git pull --ff-only …/biltoo-2718.2-world-vs-viewpoint-0de0646.bundle HEAD
```
Requires both commits from base `0de0646` (full stack in this one bundle).
