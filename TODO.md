# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2751.1-filmstrip-camera-on-scroll` (base `9395b3e`).

### Done this tip
- Filmstrip camera viewport follows Image pan/scroll: coalesce scrollbar
  `valueChanged` → `updateFilmstripChrome` (~32 ms). Zoom already emitted
  `statusChanged`; pan did not.

### Prior
- 2750.2: `-Wdouble-promotion` in ChromeColors::isDarkChrome
- 2750.1: theme-aware ChromeColors + page/cursor status terminology
- 2749.1: ThumbnailBar cursor members

### Remaining (low priority)
- Further UI copy that still says “image” where “page” is clearer
- Optional: palette-change event → force filmstrip/canvas repaint

### Apply
```bash
git pull --ff-only …/biltoo-2751.1-filmstrip-camera-on-scroll-9395b3e.bundle HEAD
```
