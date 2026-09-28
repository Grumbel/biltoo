# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2742.1-spread-destroy-center` (base `636e70e`).

### 2742.1
- Spread prune uses `destroyCanvasItem` (unregister tile LOD bags) — fixes
  ASSERT on Prev/Next tickTileLod
- Scene-only sceneRect (no view override) + deferred centerOn — centering
- Removed toolbar Back (HUD edge Up remains via setGalleryReturnAvailable)

### Apply
```bash
git pull --ff-only …/biltoo-2742.1-spread-destroy-center-636e70e.bundle HEAD
```
