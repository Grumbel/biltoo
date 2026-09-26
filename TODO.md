# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2708.1-ocr-any-image` (base `b65f69e`).

### 2708.1 — OCR any image + CMake OCR status
- Drop “document page only” gate; OCR runs on the current session image path
- biltoo CMake STATUS/WARNING for `THUMTOO_HAVE_TESSERACT` from nested thumtoo
- Panel labels: current image / multipage document

Needs **thumtoo 343.2** (plain-image rasterize + Tesseract CMake warning).

### Apply
```bash
git pull --ff-only …/thumtoo-343.2-ocr-images-tesseract-66fc03e.bundle HEAD
git pull --ff-only …/biltoo-2708.1-ocr-any-image-b65f69e.bundle HEAD
```
