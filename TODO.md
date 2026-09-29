# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2818.1-tile-jpeg-decode-pool on 2817.1 / origin+attention.

### 2818.1 Tile decode batching (host)
- JPEG→rgba for grid tiles uses a dedicated QThreadPool (max 2), not the
  global pool — Gallery multi-cell completions no longer spawn one thread each
- Requires **thumtoo-006** (`request_tiles` one batch job)

### 2817.1 Tool unification (Attention)
- Canvas tool trigger exits Attention; enter clears annot tool

### Still open (tile unification)
- PathRaster / PreferCache vs TileLoadCoordinator ownership polish
- Filmstrip cold path still uses makeThumbnail pool (warm is PreferCache)
- DjVu/EPUB multi-cell batch encode still miss→retry in thumtoo-006

### Required thumtoo
thumtoo-006.1-request-tiles-batch-job-3e6987f.bundle
