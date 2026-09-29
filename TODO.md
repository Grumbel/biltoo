# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2824.1-attention-exclusive-radio on 2823.3 stack.

### 2824.1 Attention in Exclusive canvas-tool radio
- `m_attentionAct` member of same QActionGroup as Select/Pan/Zoom/Crop/annot
- Enter/re-toggle via `onCanvasToolTriggered` (parity with Crop)
- `syncCanvasToolChrome` priority: crop > attention > annot > view
- From Gallery/Workspace: open Image mode then enable Attention

### 2823.x Gallery zoom min_scale, filmstrip PathRaster connect

### Required thumtoo
thumtoo-007.1-materialize-tile-cell-3e6987f.bundle
