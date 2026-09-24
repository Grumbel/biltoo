# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2628.1-gallery-scene-rect-override** (base `484a30a`).

### This tip — Gallery→Image→Gallery off-centre / wrong scroll area
- Root cause: `ImageController::syncImageModeSceneRect` used
  `QGraphicsView::setSceneRect` (view-level override). Gallery only updated
  `QGraphicsScene::setSceneRect`, so the tight single-image rect survived
  return and clamped scroll.
- Fix: scene is sole authority; clear view override in prepareCanvas /
  prepareModeCanvas / applyLayout / warm stash restore; syncImageModeSceneRect
  writes the scene rect only.

### Apply
```bash
git pull --ff-only …/biltoo-2628.1-gallery-scene-rect-override-484a30a.bundle HEAD
```
