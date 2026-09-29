# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2819.1-tile-rgb-from-worker on 2818 stack.

### 2819.1 Tile delivery
- requestTiles: no per-cell JPEG thread pool — host receives rgb888 from
  thumtoo workers (decode_tile_blob_to_rgb888) and only expands to rgba8
- Requires **thumtoo-007** (materialize_tile_cell + worker-side rgb)

### 2818 / 2817
- Attention exits on canvas tool; prior pool attempt removed in 2819

### Required thumtoo
thumtoo-007.1-materialize-tile-cell-3e6987f.bundle
