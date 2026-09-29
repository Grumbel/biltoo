# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2827.1-filmstrip-shrink-rebake on 2826.2 stack.

### 2827.1 Filmstrip shrink
- Rebake icons at `filmstripDecodeEdge` when strip shrinks (no large→small paint downsample)
- `setThumbnailIcon` allows smaller install when haveEdge > need
- Docs: strip is QPixmap/TileSynth, not TileSession — no tile plan overlay

### Required thumtoo
thumtoo-007.1-materialize-tile-cell-3e6987f.bundle
