# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2603.1-fix-selectiongeometry-include** (base `7d823d8`).

### Fix
`imageview_routers.cpp` used `SelectionGeometry::preferUniquePathItem` /
`unionContentAabbs` without including `item/selectiongeometry.h` (moved
with the domain co-location peel). Build broke on those two call sites.

### Prior tip (still current design direction)
Proper class split (not file moves): peel public methods off `ImageView`
onto controllers. See previous tip notes for what already moved.

### Still on ImageView (next peel candidates)
- setViewMode / setLayoutMode / reloadFromDisk (multi-mode shell)
- HUD setters (slideshow coupling in afterChange)
- statusText / appearance host gather
- DisplayPipelineHost / dual-view host surface
- QGraphicsView overrides

### Root layout note
`*/imageview_routers.cpp` remains temporary co-location of residual
`ImageView::` methods until they leave the class entirely. Prefer peeling
API + deleting methods over moving files.

### Apply
```bash
git pull --ff-only …/biltoo-2603.1-fix-selectiongeometry-include-7d823d8.bundle HEAD
```
