# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2750.2-fix-chromecolors-double-promotion` (base `9395b3e`).

### Done this tip
- Fix `-Wdouble-promotion` in `ChromeColors::isDarkChrome` (`0.5` → `0.5f`)

### Prior (2750.1)
- Theme-aware `ChromeColors` (dark/light Window palette → stroke/fill lightness)
- Filmstrip search dot + drop guide use Select/Search roles
- Nav menu: Previous/Next/First/Last **Page**; tips name **cursor**
- OCR / crop empty state: **no page under cursor**
- Select tool tip: page selection vs text-region selection
- docs/VIEW_AND_SELECTION.md status + §8 progress notes
- 2749.1: declare ThumbnailBar `m_cursorIndex` / `m_cursorViewportNorm`

### Remaining (low priority)
- Further UI copy that still says “image” where “page” is clearer
- Optional: palette-change event → force filmstrip/canvas repaint (roles
  recompute on next paint already)

### Apply
```bash
git pull --ff-only …/biltoo-2750.2-fix-chromecolors-double-promotion-9395b3e.bundle HEAD
```
