# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.10-ocr-rgb-pack` (base `d456ffc`).

### OCR / rotation (later)
Tesseract can supply orientation data we do not use yet:

1. **Page OSD** — `DetectOrientationScript` / `PageIterator::Orientation` →
   0/90/180/270° + deskew angle (`osd.traineddata`, `PSM_AUTO_OSD`). Whole-page
   upright correction before recognize.
2. **Per-line baseline** — `PageIterator::Baseline` endpoints → line angle for
   **rotated** overlay drawing. `BoundingBox` stays axis-aligned; we only store
   AABBs today, so free rotation / skew looks wrong on paint.
3. Not a substitute for biltoo content/crop transforms; paint already maps via
   `itemAppliedContentXform`.

### Fixes in this tip
- Appearance OCR: pack dense RGB888 from ARGB32 (grade/crop materialize) so
  Tesseract is not fed padded/ARGB-as-RGB (skewed garbage with colour grade).
- Grade stays in the OCR path (OCR what the user sees).

### Apply
```bash
git pull --ff-only …/biltoo-2716.10-ocr-rgb-pack-d456ffc.bundle HEAD
```
Requires thumtoo **345.2**. Flake piper: `nix flake lock --update-input text2sprech`.
