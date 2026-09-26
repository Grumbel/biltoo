# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2706.3-ocr-gen-atomic` (base `b65f69e`).

### 2706.3 — Atomic generation counters
- `m_ocrGeneration` / `m_docSearchGeneration` are `std::atomic` (worker vs GUI cancel)

### 2706.2 — Silence OCR QPointer null-deref warnings
### 2706.1 — Find source, crop suggest, OCR batch jobs

Needs thumtoo **342.2+** (prefer **343.1** for unused-rasterize fix).

### Apply
```bash
git pull --ff-only …/biltoo-2706.3-ocr-gen-atomic-b65f69e.bundle HEAD
```
