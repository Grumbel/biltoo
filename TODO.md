# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.3-text-crop-map-dpi-ui` (base `d456ffc`).

### Fixes
1. Text overlays: paint via `ContentXform::mapSourceRectToDisplay` only; scale
   into item layout size so crop apply/reset does not leave a translation error.
2. OCR panel **Source DPI** (0 = Auto, 70–600); persisted as `ocr/dpi`.
3. Requires thumtoo **345.2** (`OcrOptions::dpi` / `SetSourceResolution`).

### Apply
```bash
git pull --ff-only …/biltoo-2716.3-text-crop-map-dpi-ui-d456ffc.bundle HEAD
```
