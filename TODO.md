# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2769.1-ocr-region-y-up` (base `b8a0cf3`).

### Stack
1. 2766.1 — hide title when tabs
2. 2767.2 — filmstrip zero titlebar
3. 2768.1 — toolbar text icons
4. **2769.1** — OCR region paint uses layer.pageYUp (fixes Y-inverted OCR boxes)

### 2769.1
- regionImageRect / search / speak: honour PageTextLayer::pageYUp
- Default pageYUp false; comments aligned with MuPDF Y-down

Requires **thumtoo ≥ 352.1** for new PDF OCR layers (page_y_up false).
Existing inverted OCR: re-run OCR This Page after thumtoo update, or rely on
layer.pageYUp if the store already marked Y-up correctly.

### Apply
```bash
git pull --ff-only …/biltoo-2769.1-ocr-region-y-up-b8a0cf3.bundle HEAD
```
