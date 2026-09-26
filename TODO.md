# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2711.1-dock-layout-panels-menu` (base `b65f69e`).

### 2711.1 — Dock layout persistence + Panels menu
- Version-gated `dockLayoutState` / `dockLayoutVersion` (`kDockLayoutStateVersion = 1`).
  Bump the constant if a saved layout crashes on restore; mismatched version is ignored.
- **View → Panels** submenu: all dock toggles in one place + **Reset Panel Layout**.
- Still applies mode-specific overrides (Layout workspace-only, adjustments explicit key).

### 2710.5 — Fix selection/hover paint (regression from 2710.4)
- Selection/hover restored to: search → outlines → glyphs → selection → hover.
- Stronger cyan kept; early-out allows glyphs/selection/hover-only overlays.

### 2710.4 — Text panel recursion
- `updateTextPanel` no longer refresh()es while handling `layerChanged`.

Needs thumtoo **343.6** for EPUB OCR.

### Apply
```bash
git pull --rebase …/biltoo-2711.1-dock-layout-panels-menu-b65f69e.bundle HEAD
```

---

## Roadmap / later

### 0.3.0 — KDDockWidgets (optional)
Only if QMainWindow docks prove insufficient (nested docking, advanced layouts).
Spike against nixpkgs first; keep version-gated save/restore either way.
