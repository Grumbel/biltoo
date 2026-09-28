# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2799.1-fix-toolbar-break-strip` (base `a989daf`).

### 2799.1
- Thin grey strip under main toolbar: caused by empty Location/Search toolbar
  *rows* (`addToolBarBreak` while bars hidden)
- `rebuildAuxiliaryTopToolBars()`: `removeToolBar` when hidden; only
  `addToolBarBreak`+`addToolBar` while pinned or transient (Ctrl+L / Ctrl+F)

### Prior
- 2798 fullscreen all docks
- 2797 location/search fullscreen hide

### Apply
```bash
git pull --ff-only …/biltoo-2799.1-fix-toolbar-break-strip-a989daf.bundle HEAD
```
