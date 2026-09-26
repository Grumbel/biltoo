# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2707.3-ocr-error-detail` (base `b65f69e`).

### 2707.3 — Specific OCR failure messages
- `ThumtooCache::OcrRunResult` + `runOcrPageTextLayer` / `ocrAvailable`
- Status: NoClient / BadUri / Unavailable / Failed / EmptyText / Ok
- Panel log + status bar use `result.message()`; document OCR logs first failures

### Apply
```bash
git pull --ff-only …/biltoo-2707.3-ocr-error-detail-b65f69e.bundle HEAD
```
