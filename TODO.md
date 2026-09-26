# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2706.2-ocr-null-warnings` (base `b65f69e`).

### 2706.2 — Silence OCR QPointer null-deref warnings
- GUI/worker OCR lambdas use `MainWindow *host = self.data()` before member access

### 2706.1 — OCR follow-ups (Find source, crop suggest, batch jobs)
- Find bar Auto/Native/OCR; TextLayerResolve; crop suggest; OCR jobs

Needs thumtoo **342.2+** (region kinds). Prefer **343.1** if pulling the unused-rasterize warning fix.

### Apply
```bash
git pull --ff-only …/biltoo-2706.2-ocr-null-warnings-b65f69e.bundle HEAD
```
