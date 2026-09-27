# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.9-tile-path-single` (base `bcbb97e`).

### 2714.9 — Single warm tile path (no virtual special-case tiles)
Architecture:
- **Warm tiles** → only `ImageItem` + `TileLodController` + shared
  `paintTilePlanDebugOverlay` (TILE / s=N / COMPLETE).
- **Virtual slots** → **cold underlay only** (LQIP / EMB / placeholder).
- `syncVirtualWindow` prioritizes creating live items for on-screen paths with
  process or durable tiles (budget no longer skips warm creates).
- `ImageCache::matchNativeAspect` — one upright-helper for EXIF thumb vs Store size.

Removed the dual `prepare_and_paint_cover` path from Gallery virtual paint
(was a second product path with incomplete orient/debug).

### Prior (included)
2714.8 shared overlay · 2714.7–1 …

### Apply
```bash
git pull --ff-only …/biltoo-2714.9-tile-path-single-bcbb97e.bundle HEAD
```
