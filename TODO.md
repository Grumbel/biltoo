# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2712.7-gallery-zoom-region-center` (base `b65f69e`).

### 2712.7 — Gallery Zoom tool region centers correctly
- Zoom-tool rubber `fitInView` was pinned top/left by Gallery AlignCenter +
  bar-range recenter. Now uses NoAnchor + `centerOn(target)` and the same
  interactive-transform suppress path as Ctrl/Pan wheel zoom.

### 2712.6 — Pan tool wheel zoom (Gallery)
### 2712.5 — Multi-page text selection

### Apply
```bash
git pull --rebase …/biltoo-2712.7-gallery-zoom-region-center-b65f69e.bundle HEAD
```
