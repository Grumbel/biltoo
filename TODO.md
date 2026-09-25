# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2667.1-pdf-crop-tile-viewport` (base `2e49220`).

### Stack
1. 2663.1 — Open Selection content snapshot
2. 2664.1 — Menu Document/Thumbs flatten
3. 2665.1 — orientedCropRect for tile *paint*
4. 2666.1 — Docs: kill Soft backlog
5. **2667.1** — mapDisplayRectToSource + tileNativeSize: same crop scale for tile *viewport*

### This tip
- First crop-tile fix scaled crop for paint only; plan viewport still used raw
  cropRect (soft basis) → wrong source region / density on PDF pages.
- `mapDisplayRectToSource` now uses `orientedCropRect`.
- Never treat post-crop `imageSize()` as tile-grid native.

### Apply
```bash
git pull --ff-only …/biltoo-2667.1-pdf-crop-tile-viewport-2e49220.bundle HEAD
ctest -R contentxform --output-on-failure
```
