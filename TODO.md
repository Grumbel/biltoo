# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.2-ocr-source-dpi` (base `d456ffc`).

### OCR
1. Regions in page space only; crop is paint-time (`pageYUp` from layer).
2. Appearance OCR passes **source DPI** (`72×native/pageBounds` or 300) so
   Tesseract does not treat a crop as a 72-DPI scrap.
3. Requires thumtoo **345.2-ocr-source-dpi**.

### Apply
```bash
git pull --ff-only …/biltoo-2716.2-ocr-source-dpi-d456ffc.bundle HEAD
```
