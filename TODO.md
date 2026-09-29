# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2811.1-canvas-tool-palette` (base `2085c07`).

### 2811.1
- Tool palette unification: one ExclusiveOptional QActionGroup for Select/Pan/Zoom
  + all annotation tools; `onCanvasToolTriggered` owns activation
- Mode chrome sync mirrors annot tool checks; docs/TOOL_UNIFICATION.md updated

### Prior (2810.5)
- Unit tests: mapDisplayPointToSource ↔ mapSourcePointToDisplay round-trip

### Next
- Run contentxform_test locally
- Manual: rotate 90° + pen; Gallery non-primary tile; tool radio (Select ↔ Pen ↔ Annot-Select)
- Optional: fold Crop/Attention into the canvas radio

### Apply
```bash
git pull --ff-only …/biltoo-2811.1-canvas-tool-palette-2085c07.bundle HEAD
```
