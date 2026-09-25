# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2673.1-tile-overlap-paint` (base `2e49220`).
**thumtoo:** `thumtoo-340.1-tile-overlap-5e47314` → `f0c0055`.

### Tile bilinear seams
- thumtoo encodes 1px right/bottom overlap (`kTileOverlap`); grid step stays 256.
- biltoo expands dest from bitmap size when payload > exclusive cell.
- Old 256×256 Store tiles still paint; re-prepare/purge for full seam quality.
- See thumtoo `docs/TILE_OVERLAP.md`.

### Apply
```bash
# thumtoo tip into flake / vendor, then:
git pull --ff-only …/biltoo-2673.1-tile-overlap-paint-2e49220.bundle HEAD
```
