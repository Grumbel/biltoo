# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2665.1-pdf-crop-tile-scale` (base `2e49220`).

### Stack
1. 2663.1 — Open Selection content snapshot
2. 2664.1 — Menu Document/Thumbs flatten
3. **2665.1** — PDF crop + tiles: scale cropRect via cropSourceSize → page native

### This tip
- Tile paint used recorded cropRect (often soft-sample basis) against
  page-native tile grid → squished PDF crops; soft materialize was fine.
- `ContentXform::orientedCropRect` shared by layoutSize, mapSourceRectToDisplay,
  and ImageItem tile paint.
- Unit tests: orientedCropRect soft→native.

### Apply
```bash
git pull --ff-only …/biltoo-2665.1-pdf-crop-tile-scale-2e49220.bundle HEAD
ctest -R contentxform --output-on-failure
```
