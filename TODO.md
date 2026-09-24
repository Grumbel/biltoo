# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2468.1-own-image-size-coordinator** (base `7d823d8`).

### Ownership transfer
- **2464–2466:** Workspace group / page-guide / chrome + ItemInteract
- **2467:** Image-mode framing → ImageController
- **2468:** ImageSizeBook + probe/remember policy → `ImageSizeCoordinator`
  (`item/imagesizecoordinator.*`). GallerySizeResolve host + applyProbedImageSize
  stay on ImageView (canvas apply).

### Next
- GallerySizeResolveHost → GalleryController (optional)
- Dual PreferCache coordination (optional)

### Apply
```bash
git pull --ff-only …/biltoo-2468.1-own-image-size-coordinator-7d823d8.bundle HEAD
```
