# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2833.1-tool-palette-attention-hud (on 2832.5 stack).

### Tool unification (2833.1)
- Attention on left Tools strip (Exclusive radio parity with Crop)
- HUD shortcut: Shift+H; Pan keeps H (GIMP Hand) — clash resolved
- Docs: TOOL_UNIFICATION.md table + shortcuts; setTool (not setCurrentTool)

### Prior: Filmstrip tiles (shared cover path)
- Paint: `prepare_and_paint_cover` + plan overlay inside cover
- Issue: `scheduleFilmstripTiles` → TileLodController::tick
- Underlay: EMB/LQIP pixmap only when tiles not drawn
- LOD map trimmed to visible paths each surface tick
- No TileSynth / PreferCache dual climb for strip display

### Required thumtoo
thumtoo-008.1-markdown-cmark-mutex-3e6987f.bundle

### Possible next
- Text Highlighter → Mark selection on Annotations panel (design only; see ANNOTATION_OVERLAY §14)
- DisplaySurface AttachSoft/Full on filmstrip still copies ImageCache into icons (underlay); could skip when tiles cover
- Image mode oriented paint remains `paint_tiles_display` (correct twin of cover)
- Workspace free rotation/shear of items vs content orient — needs testing
