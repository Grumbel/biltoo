# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2689.1-gallery-wheel-zoom-cursor` (base `932ed5c`).

### 2689 — Gallery Ctrl+wheel zooms about cursor
`onScrollBarRangeChanged` was `centerOn(viewport centre)` after zoom-induced
range changes, cancelling `AnchorUnderMouse`. Suppress bar-range recenter
during wheel zoom; zoom with NoAnchor + translate delta under cursor.

### Apply
```bash
git pull --ff-only …/biltoo-2689.1-gallery-wheel-zoom-cursor-932ed5c.bundle HEAD
```
