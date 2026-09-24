# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2469.1-own-gallery-size-resolve** (base `7d823d8`).

### Ownership transfer
- **2464–2468:** Workspace / framing / ImageSizeCoordinator
- **2469:** `GallerySizeResolveHost` + `GallerySizeResolve` owned by
  `GalleryController` (`gallerycontroller_sizeresolve.cpp`). ImageView is no
  longer the size-gate host; `hostGallerySizeResolve()` forwards to gallery.

### Next
- paint collaborator (optional)
- Dual PreferCache coordination (optional)

### Apply
```bash
git pull --ff-only …/biltoo-2469.1-own-gallery-size-resolve-7d823d8.bundle HEAD
```
