# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2832.5-filmstrip-lod-trim on 2832.4 stack.

### Filmstrip tiles (shared cover path)
- Paint: `prepare_and_paint_cover` + plan overlay inside cover
- Issue: `scheduleFilmstripTiles` → TileLodController::tick
- Underlay: EMB/LQIP pixmap only when tiles not drawn
- LOD map trimmed to visible paths each surface tick
- No TileSynth / PreferCache dual climb for strip display

### Required thumtoo
thumtoo-008.1-markdown-cmark-mutex-3e6987f.bundle

### Possible next
- DisplaySurface AttachSoft/Full on filmstrip still copies ImageCache into icons (underlay); could skip when tiles cover
- Image mode oriented paint remains `paint_tiles_display` (correct twin of cover)
