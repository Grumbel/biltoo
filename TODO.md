# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2811.3-canvas-tool-sync` (base `2085c07`).

### 2811.3
- Verify/harden tool path: `syncCanvasToolChrome()` shared by mode UI
- set*Tool uses setChecked + onCanvasToolTriggered (not trigger toggle)
- Docs: activation path + why Exclusive not Optional

### 2811.2
- Exclusive unified group; force single checked action

### 2811.1
- Tool palette unification

### Next
- Manual GUI: Select → Pan → Zoom → Select; Pen → Select; V/H/Z shortcuts
- Run contentxform_test locally
- Optional: fold Crop/Attention into the canvas radio

### Apply
```bash
git pull --ff-only …/biltoo-2811.3-canvas-tool-sync-2085c07.bundle HEAD
```
