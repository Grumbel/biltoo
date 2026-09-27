# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.19-ocr-appearance` (base `d456ffc`).

### OCR matches view
- Crop (2715.18) + **orient/colour grade** via materializeDisplay → `ocr_rgb_page_text_layer`.
- thumtoo: `thumtoo-ocr-rgb-4d49372.bundle` (crop + RGB OCR API).

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-ocr-rgb-4d49372.bundle HEAD
# rebuild thumtoo
git -C biltoo pull --ff-only …/biltoo-2715.19-ocr-appearance-d456ffc.bundle HEAD
```
