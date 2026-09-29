# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2816.5-filmstrip-shared-cache on origin via 2816.4.

### 2816.5 Filmstrip
- Warm visible rows: ImageCache + PreferCache/TileSynth only (no makeThumbnail pool job)
- Drop QThreadPool hasDurableTiles discovery per row
- Surface tick 200ms while awaiting, 1500ms idle
- Concurrent thumb jobs default 12 (cold path only)

### Not yet
- Tile plan overlay is ImageItem/canvas only; filmstrip is QPixmap icons
- Full paint-path unification (ImageItem in strip) is a larger refactor

### Required thumtoo
thumtoo-005-no-interactive-tile-batch-551a360.bundle

