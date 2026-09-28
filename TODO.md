# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2808.6-sticky-zoom-resize-noloop` (base `2085c07`).

### 2808.6
- Fix infinite Resize loop: onViewResized must not call applyImageModeFraming
  (refreshScrollBarGeometry toggles policy → resize). Sticky Fit/Fill use
  fitItem only; re-entry guards on resize and scrollbar refresh.

### Apply
```bash
git pull --ff-only …/biltoo-2808.6-sticky-zoom-resize-noloop-2085c07.bundle HEAD
```
