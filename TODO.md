# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2817.1-attention-exits-on-canvas-tool on 021b919 (2816.5 filmstrip).

### 2817.1 Tool unification (Attention)
- `onCanvasToolTriggered` exits Attention mode (was only cleared on crop enter / mode switch)
- Entering Attention clears active annotation tool + cancels crop (chrome resync)
- Docs: TOOL_UNIFICATION.md Attention contract + open items (fold into radio, H clash)

### Prior (2816.5 Filmstrip)
- Warm visible rows: ImageCache + PreferCache/TileSynth only
- Surface tick 200ms awaiting / 1500ms idle

### Still open (tool unification)
- Text Highlighter vs Mark selection (design only — ANNOTATION_OVERLAY §14)
- Optional: fold Attention into Exclusive radio
- Shortcut: Pan and HUD both claim `H`
- Workspace free rotation/shear testing; Gallery rubber-band shapes

### Required thumtoo
thumtoo-005-no-interactive-tile-batch-551a360.bundle
