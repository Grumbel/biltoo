# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2821.1-filmstrip-shared-pixel-path on 2820 stack.

### 2821.1 Filmstrip pixel path
- Visible loads: PreferCache/TileSynth only (`scheduleFilmstripTilePixels`)
- Ladder delivery installs on GUI (no per-row QThreadPool)
- Cold path no longer `makeThumbnail` on the global pool
- `makeThumbnail` remains ImageCache→icon helper only

### 2820.1
- Fix raw NUL in BILTOO_FOCUSFULL check

### Required thumtoo
thumtoo-007.1-materialize-tile-cell-3e6987f.bundle
