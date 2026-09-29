# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2811.2-canvas-tool-exclusive` (base `2085c07`).

### 2811.2
- Fix stuck Select/Pan/Zoom: Exclusive (not Optional) unified group; force single
  checked action in onCanvasToolTriggered; set*Tool uses QAction::trigger()

### 2811.1
- Tool palette unification: one QActionGroup for view + annotation tools

### Next
- Manual: Select → Pan → Zoom → Select radio; Pen → Select; shortcuts V/H/Z
- Run contentxform_test locally
- Optional: fold Crop/Attention into the canvas radio

### Apply
```bash
git pull --ff-only …/biltoo-2811.2-canvas-tool-exclusive-2085c07.bundle HEAD
```
