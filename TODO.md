# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2722.1-pdf-text-y-down-fallback` (base `36701f2`).

### 2722.1 — PDF text Y axis (with thumtoo 346.1)
- Root cause: MuPDF stext/page space is **top-left Y-down**, but layers were
  tagged `page_y_up=true` (PDF *file* space). Host flip made boxes upside-down.
- Requires **thumtoo 346.1** (`page_y_up=false` on PDF/EPUB native+OCR).
- biltoo `pageSpaceYUpForPath`: only DjVu → true; PDF/EPUB → false.
- Re-extract or clear cached text layers written with the old flag.

### Apply
```bash
git pull --ff-only …/biltoo-2722.1-pdf-text-y-down-fallback-36701f2.bundle HEAD
```
Requires thumtoo **346.1**.
