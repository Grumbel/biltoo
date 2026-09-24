# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2487.1-own-path-raster-on-pipeline** (base `7d823d8`).

### Ownership transfer
- **PathRasterService** on DisplayPipelineController (parented to hostObject)
- ImageView hostPathRaster() → m_displayPipeline->pathRaster()

### Residual on ImageView (intentional shell/host)
ViewFraming, ViewMode, CanvasBackground, ViewportChrome, SessionIdentity,
SessionBindBook, TileNeighborPrefetch, ImageModeSoftProvider, PerfStats

### Apply
```bash
git pull --ff-only …/biltoo-2487.1-own-path-raster-on-pipeline-7d823d8.bundle HEAD
```
