# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2706.1-ocr-find-crop-batch` (base `b65f69e`).

### 2706.1 — OCR follow-ups (Find source, crop suggest, batch jobs)
- Find bar: **Auto / Native / OCR** text-layer source (`find/textSource`); document scan uses the same policy via `TextLayerResolve`
- Crop panel: **Suggest from headers/footers** (Header/Footer/PageNumber bands → manual top/bottom margins)
- OCR Document: concurrent workers (`ocr/jobs`, default 2, max 4); progress shows done/ok/failed

Needs thumtoo **342.2** (region kinds).

### Apply
```bash
git pull --ff-only …/biltoo-2706.1-ocr-find-crop-batch-b65f69e.bundle HEAD
```
