# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2649.2-crop-panel-autocrop-segv` (base `9740316`).

### Done
- Orient / colour multi-apply; reset appearance GUI fix (2646–2648).
- **Crop panel v1 (2649.1)** + **autocrop SEGV fix (2649.2):**
  - Batch apply uses a pure **plan phase** (no ItemWorld/pixel mutation while
    collecting targets), then applies.
  - Autocrop samples from **item pixels only** (deep `.copy()`); no
    `ImageCache::get` during batch (was SEGV in `qHash(path)` under selection).
  - `QPointer<ImageItem>` skip if tile dies mid-apply.

### Next
1. Crop panel polish: live preview; normalised fields; status when size unknown.
2. Template / even-odd / stack (later).

### Apply
```bash
git pull --ff-only …/biltoo-2649.2-crop-panel-autocrop-segv-9740316.bundle HEAD
```
