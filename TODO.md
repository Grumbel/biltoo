# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2602.1-peel-transform-workspace-api** (base `7d823d8`).

### Proper class split (not file moves)
Co-locating `ImageView::` routers into domain dirs was **premature** —
the class still owned the API surface. This tip starts **peeling** public
methods off `ImageView` onto controllers that already own the bodies.

### Peeled off ImageView public API → call controllers instead

| Was `ImageView::` | Now call |
|-------------------|----------|
| flip / rotate / zoom / sticky / armZoomRegion | `hostImage().…` (`ImageController`) |
| resetContentAppearanceForTargets | `hostImage().…` |
| setWorkspacePaths / addImageForSession / placeOrMoveImageAt | `hostWorkspace().setPaths` / `…` |
| setTool | `hostWorkspace().setTool` |

MainWindow + Gallery/Crop internal callers updated.
`commitItemSessionEdit` stays on ImageView as `DisplayPipelineHost` override
(routes to ImageController).

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
git pull --ff-only …/biltoo-2602.1-peel-transform-workspace-api-7d823d8.bundle HEAD
```
