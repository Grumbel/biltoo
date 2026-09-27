# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.5-ocr-always-run` (base `d456ffc`).

### Changes
- Run OCR always re-runs Tesseract (no Force checkbox / Re-OCR menu / skip-cache).
- Text overlays: ContentXform map + layout scale; OCR Source DPI in panel.
- Requires thumtoo **345.2**.

### Apply
```bash
git pull --ff-only …/biltoo-2716.5-ocr-always-run-d456ffc.bundle HEAD
```
