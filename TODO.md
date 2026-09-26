# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2705.1-ocr-this-page` (base `6c3e877`).

### 2705.1 — OCR this page (thumtoo Tesseract)
- Requires thumtoo with `ensure_ocr_page_text_layer` (tip 342.1)
- `ThumtooCache::ensureOcrPageTextLayer`
- `TextLayerController::applyOcrLayer`
- View → **OCR This Page** (wait cursor; enables Show Text Regions on success)

### Next
- Batch OCR document + language picker
- Prefer OCR vs native in Find when user chooses

### Apply
```bash
git pull --ff-only …/biltoo-2705.1-ocr-this-page-6c3e877.bundle HEAD
```
