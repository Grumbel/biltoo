# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2822.1-filmstrip-pathraster on 2821 stack.

### 2822.1 PathRaster is the filmstrip climb
- `scheduleFilmstripTilePixels` → `PathRasterService::ensure(TileDisplay)` when wired
- PathRaster pump: durable → TileSynth; cold → PreferCache overview (`scheduleDisplayPixels`)
- No silent scheduleTiles no-op that left cold climbs with zero work
- MainWindow wires ImageView’s PathRaster + rasterImproved → strip reload

### 2821.1 Filmstrip pixel path
- No per-row QThreadPool makeThumbnail

### Required thumtoo
thumtoo-007.1-materialize-tile-cell-3e6987f.bundle
