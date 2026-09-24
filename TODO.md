# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2454.1-fix-maptoscene-tilecoord** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake: CLOSED**
- **Dual ImageView Stage 0–2b: LANDED** — pipeline bound only to DisplayPipelineHost

### Stage 2b summary
- Dropped `m_view` / `view()` from DisplayPipelineController
- Ctor: `DisplayPipelineController(DisplayPipelineHost *host)`
- ImageView constructs with `this` (is the host)

### Fix 2454 (build)
- `using QGraphicsView::mapToScene` so QRect/QPolygonF overloads stay visible
- `TileLoadCoordinator` includes Crop/Slideshow/GallerySizeResolve (complete types)
- Construct coordinator with `&m_displayPipeline` (not `ImageView*`)
- Drop unused viewport local in coordinator tick

### Verification (static)
| Check | Result |
|-------|--------|
| ImageItem friends | sole DisplayPipelineController |
| Pixel mutators outside pipeline | none |
| Pipeline `m_view` / `view()` | **removed** |
| `QPointer<ImageView>` in display/ | **0** |
| Host pure virtuals | ~105 |
| mapToScene(QRect) call sites | compile (using) |
| TileLoadCoordinator ctor | DisplayPipelineController* |
| Runtime / nix build | not run in sandbox |

### Next
- Stage 2c: dual-pane shell + active host / shared ItemWorld
- Optional: paint/input Host extraction
- Confirm full rebuild on host

### Apply
```bash
git pull --ff-only …/biltoo-2454.1-fix-maptoscene-tilecoord-7d823d8.bundle HEAD
```
