# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2705.2-ocr-batch-async` (base `6c3e877`).

### 2705.2 — OCR batch + async
- **OCR This Page**: `QThreadPool` worker + install layer on GUI
- **OCR Document…**: language prompt (QSettings `ocr/lang`), all pages from
  `documentPagePathsForSearch()`, centre progress + status, cancel
- **Cancel OCR** stops batch via generation counter
- `ensureOcrPageTextLayer(..., lang)`, `installLayer` for worker results
- Needs thumtoo **342.1** OCR APIs

### Next
- Force re-OCR option; skip-empty stats
- Semantic header/page-number tags
- Prefer OCR vs native in Find when user chooses

### Apply
```bash
git pull --ff-only …/biltoo-2705.2-ocr-batch-async-6c3e877.bundle HEAD
```
