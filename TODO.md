# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2785.1-annot-multiselect-prefs` (base `a989daf`).

### 2785.1 — Multi-select, layer visibility, tool prefs
- Select: Shift=add, Ctrl=toggle, Ctrl+A=select all on page; Escape/Delete unchanged
- Show Annotations (Image + View menus); `setLayerVisible` + QSettings
- Colour and width persisted under `annotation/` QSettings; tool switch no longer
  overwrites colour (width still tool-appropriate defaults)

### Prior
- 2784.1 formatVersion 2, ReplaceCommand, sticky text field, load clear
- 2783.1 compile fix

### Apply
```bash
git pull --ff-only …/biltoo-2785.1-annot-multiselect-prefs-a989daf.bundle HEAD
```

### Still open
- Move/resize shapes after place
- Content-hash binding of pageBounds
- AnnotationPainter split from controller
- PDF /Annot export
