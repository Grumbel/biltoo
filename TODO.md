# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2638.1-gallery-bars-preserve-center** (base `d69d58e`).

### This tip — layout full → bars appear → off-centre
Root: after AlwaysOn pack measure, sceneRect was expanded to the *no-bar*
viewport, then AsNeeded bars appeared and shrank the client under AlignCenter.
Also refreshScrollBarGeometry (Off↔AsNeeded toggle) shifted the view without
preserving the scene centre.

- When bars can show (AsNeeded/AlwaysOn): keep tight pack sceneRect (no expand)
- AlwaysOff only: expand to fill the client
- refreshScrollBarGeometry: centerOn(mapToScene(viewport.center)) after toggle

### Apply
```bash
git pull --ff-only …/biltoo-2638.1-gallery-bars-preserve-center-d69d58e.bundle HEAD
```
