# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2798.1-fullscreen-all-docks` (base `a989daf`).

### 2798.1
- Fullscreen closes *all* tool docks (OCR, Crop, Annotations, Text, TOC, Messages, …)
  and restores prior open state on leave (was only a subset → OCR stayed open)
- KDDock separator thickness 3 (slightly thinner grips)

### Note — thin top grey strip
Still under investigation: few pixels at top of canvas in window *and* fullscreen.
Not Location/Search alone (present when those are hidden). Suspects: residual
QMainWindow toolbar-break row, KDDock central frame edge, or DE chrome in
screenshots. Not fixed as a definite root cause yet.

### Apply
```bash
git pull --ff-only …/biltoo-2798.1-fullscreen-all-docks-a989daf.bundle HEAD
```

### Deferred
- PDF source write-back — docs/PDF_SOURCE_WRITEBACK.md
