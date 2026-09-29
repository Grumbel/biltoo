# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2815.2-messagelog-include` (base `2085c07`).

### 2815.2
- Include `messagelogpanel.h` in `mainwindow_session.cpp` so `reportSessionOpenFailed` can call `appendError` (incomplete type fix)

### Prior
- 2815.1 annotated-pages list + cutouts design note; `reportSessionOpenFailed` body
- 2814.4 OCR This Page opens panel only
- 2814.3 surface failed Open/Bookshelf (declaration without definition — fixed in 2815.1)

### Apply
```bash
git pull --ff-only …/biltoo-2815.2-messagelog-include-2085c07.bundle HEAD
```
