# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2811.6-crop-leave-chrome-sync` (base `2085c07`).

### 2811.6
- cropModeChanged → syncCanvasToolChrome (Apply/Cancel left Crop unchecked and no view tool pressed under Exclusive)
- activateDefaultViewTool re-syncs after handler

### Prior
- 2811.5 crop exit → mode-default tool (Pan in Image)
- 2811.4 Crop in Exclusive radio

### Next
- Manual: Image Pan → Crop → Apply/Cancel → Pan button pressed
- Optional: fold Attention into the canvas radio

### Apply
```bash
git pull --ff-only …/biltoo-2811.6-crop-leave-chrome-sync-2085c07.bundle HEAD
```
