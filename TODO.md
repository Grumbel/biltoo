# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2832.1-filmstrip-tile-cover on 2831.1 stack.

### 2832.1 Filmstrip draws real tiles
- Per-path TileLodController + prepare_and_paint_cover (shared registry)
- scheduleFilmstripTilePixels issues via lod->tick (not TileSynth)
- Pixmap underlay only when tiles not drawn yet

### Required thumtoo
thumtoo-008.1-markdown-cmark-mutex-3e6987f.bundle
