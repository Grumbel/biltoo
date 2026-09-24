# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2599.1-live-meta-commit** (base `7d823d8`).

### Ownership transfer
- **ImageController** owns:
  - `applyState` / `syncLiveContentMetaFromState` / `syncLiveColorFromState`
  - `clearLiveContentMeta`
  - `persistGeometrySessionState`
  - `commitItemSessionEdit` (persist + peer sync + propagate + status)
  - `targetHasContentAppearance`
- ImageView methods are thin routers

### Prior
**2598.1** Workspace owns pending bind take; test appearance stub  
**2597.1** ImageController owns rememberItemState + appearance propagate  
**2596.1** Pipeline owns high-res export paint + content bounds

### Residual on ImageView (intentional host / shell)
- **setViewMode** mode shell (leave/enter orchestration) — keep on ImageView
- **captureState** / **freezeItemAppearance** / **contentLayoutSize** — host gather for SessionAppearance pure helpers
- **statusText** / **hudFileName** — host gather for HudModel pure helpers
- **renderExportImage** — canvas bg + pipeline paint orchestration
- **pendingDecodeCount** — sum of mode controllers
- QGraphicsView overrides, public MainWindow API surface, member bags

### Next thinning candidates
- statusText* / hudFileName further pure assembly on HudModel
- freezeItemAppearance already thin
- setViewMode remains intentional mode shell

### Apply
```bash
git pull --ff-only …/biltoo-2599.1-live-meta-commit-7d823d8.bundle HEAD
```
