# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2705.3-ocr-force-prefer` (base `6c3e877`).

### 2705.3 — Force re-OCR, prefer OCR layer, region kinds
- View → **Re-OCR This Page** (force=true)
- OCR Document asks whether to force re-OCR cached pages
- `refresh()` prefers cached OCR layer over native when present
- `cachedOcrPageTextLayer`; region Kind (Body/PageNumber/Header/Footer)
- Needs thumtoo **342.2**

### Apply
```bash
git pull --ff-only …/biltoo-2705.3-ocr-force-prefer-6c3e877.bundle HEAD
```
