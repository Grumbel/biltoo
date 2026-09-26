# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2710.5-fix-selection-paint-order` (base `b65f69e`).

### 2710.5 — Fix selection/hover paint (regression from 2710.4)
- 2710.4 inserted selection/hover *inside* the glyph loop → wrong transform,
  drawn once per region, only when glyphs on. Restored clean order:
  search → outlines → glyphs → selection → hover.
- Stronger cyan (pen + fill) kept so selection stays readable over glyph paper.
- Early-out also allows glyphs-only / selection-only / hover-only overlays.

### 2710.4 — Text panel recursion + selection over glyphs
- `updateTextPanel` no longer calls `refresh` while handling `layerChanged`
  (was infinite: refresh → emit → updateTextPanel → refresh…)
- (Selection paint was attempted here but the edit was corrupted; fixed in 2710.5.)

Needs thumtoo **343.6** for EPUB OCR.

### Apply
```bash
git pull --ff-only …/biltoo-2710.5-fix-selection-paint-order-b65f69e.bundle HEAD
```

---

## Roadmap / later

### 0.3.0 — Dock layout persistence (evaluate KDDockWidgets)

See prior note: careful `saveState` vs nixpkgs `kddockwidgets` spike-first.
