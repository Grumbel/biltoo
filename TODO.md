# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2750.1-theme-chrome-status-terms` (base `9395b3e`).

### Done this tip
- Theme-aware `ChromeColors` (dark/light Window palette → stroke/fill lightness)
- Filmstrip search dot + drop guide use Select/Search roles (no hard-coded /
  Qt Highlight leftovers)
- Nav menu: Previous/Next/First/Last **Page**; tips name **cursor**
- OCR / crop empty state: **no page under cursor** (not “selected”)
- Select tool tip: page selection vs text-region selection
- docs/VIEW_AND_SELECTION.md status + §8 progress notes

### Prior
- 2749.1: declare ThumbnailBar `m_cursorIndex` / `m_cursorViewportNorm`
- P0–P5 view/selection language + filmstrip chrome

### Remaining (low priority)
- Further UI copy that still says “image” where “page” is clearer
- Optional: palette-change event → force filmstrip/canvas repaint (roles
  recompute on next paint already)

### Apply
```bash
git pull --ff-only …/biltoo-2750.1-theme-chrome-status-terms-9395b3e.bundle HEAD
```
