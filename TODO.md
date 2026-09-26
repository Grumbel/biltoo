# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2707.1-ocr-panel` (base `b65f69e`).

### 2707.1 — OCR dock panel
- View → **Show OCR Panel** (replaces three similar OCR menu items as primary UI)
- Scope (page / document), language, jobs, force, Run/Cancel
- Progress bar, summary, timestamped log, current-page native/OCR region counts
- Legacy OCR menu actions still exist for shortcuts; panel is the control surface

### Prior
- 2706.x Find source, crop suggest, batch jobs, build/race fixes

Needs thumtoo **342.2+** (prefer **343.1**).

### Apply
```bash
git pull --ff-only …/biltoo-2707.1-ocr-panel-b65f69e.bundle HEAD
```
