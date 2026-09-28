# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2752.1-chrome-color-prefs` (base `9395b3e`).

### Done this tip
- Default **Select** fill/stroke shifted to brighter cyan (`#00d2ff` / `#00a5e6`)
  so selection no longer blends into grey filmstrip/canvas chrome
- `ChromeColors` is a runtime store (fill/stroke bases per role); still used
  via `selectFill(alpha)` etc.
- Preferences → Interface → **Chrome (cursor / selection)**: fill+stroke
  pickers for Select, View, Activity, Search with per-row reset
- QSettings keys `chromeSelectFill` … `chromeSearchStroke`; load on
  readSettings, save on writeSettings, apply on Preferences OK + repaint

### Prior
- 2751.1: filmstrip camera viewport follows pan/scroll
- 2750.x: theme-aware → superseded by configurable bases; terminology polish
- 2749.1: ThumbnailBar cursor members

### Apply
```bash
git pull --ff-only …/biltoo-2752.1-chrome-color-prefs-9395b3e.bundle HEAD
```
