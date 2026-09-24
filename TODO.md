# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2480.1-own-gallery-layout-debounce-timer** (base `7d823d8`).

### Ownership transfer
- **Layout debounce QTimer** on GalleryController (parented to ImageView)
- `requestDebouncedPack` / `stopLayoutDebounceTimer` on GalleryController
- ImageView: thin `requestDebouncedGalleryPack` / `stopDeferredPacking`

### Apply
```bash
git pull --ff-only …/biltoo-2480.1-own-gallery-layout-debounce-timer-7d823d8.bundle HEAD
```
