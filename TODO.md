# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2801.1-scroll-hud-fixed` (base `a989daf`).

### 2801.1
- Gallery BoundingRect blit was dragging HUD/selection overlays with the tiles
- `ImageView::scrollContentsBy`: after blit-mode scroll, `viewport()->update()`
  so drawForeground chrome stays device-fixed / rebinds to the new view

### Apply
```bash
git pull --ff-only …/biltoo-2801.1-scroll-hud-fixed-a989daf.bundle HEAD
```
