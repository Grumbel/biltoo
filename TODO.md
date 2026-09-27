# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.18-ocr-crop` (base `d456ffc`).

### OCR + crop
- Page OCR passes session crop (page space) into thumtoo `OcrOptions`.
- Needs thumtoo tip with `OcrOptions.has_crop` (`thumtoo-ocr-crop-4d49372.bundle`).

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-ocr-crop-4d49372.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2715.18-ocr-crop-d456ffc.bundle HEAD
```
