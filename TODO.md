# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2710.4-textpanel-loop-selection` (base `b65f69e`).

### 2710.4 — Text panel recursion + selection over glyphs
- `updateTextPanel` no longer calls `refresh` while handling `layerChanged`
  (was infinite: refresh → emit → updateTextPanel → refresh…)
- Selection/hover painted **after** glyph paper fill (cyan stays readable)

Needs thumtoo **343.6** for EPUB OCR.

### Apply
```bash
git pull --ff-only …/biltoo-2710.4-textpanel-loop-selection-b65f69e.bundle HEAD
```

---

## Roadmap / later

### 0.3.0 — Dock layout persistence (evaluate KDDockWidgets)

See prior note: careful `saveState` vs nixpkgs `kddockwidgets` spike-first.
