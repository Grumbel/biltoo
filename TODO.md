# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.12-contact-strip-layouts` (base `d456ffc`).

### Gallery layouts (done this tip)
- **Contact sheet** (was Flow): ordered wrap, **one global scale**, last row left-aligned.
- **Strip rows** (was Flow Fill): ordered wrap, **uniform row height**, no orphan stretch.
- Masonry family left as-is. Facing unchanged.

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
git pull --ff-only …/biltoo-2716.12-contact-strip-layouts-d456ffc.bundle HEAD
```
Requires thumtoo **345.2**.
