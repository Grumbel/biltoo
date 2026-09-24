# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2472.1-own-gallery-decode-book** (base `7d823d8`).

### Ownership transfer
- **GalleryDecodeBook** moved from ImageView to GalleryController
- `hostGalleryDecodeBook()` forwards to `m_gallery.decodeBook()`
- ImageView / pipeline / shell keep host surface; Gallery uses `m_decodeBook` directly

### Prior
- 2471: Workspace page-guide/group/item input ownership
- 2471.2: moc fix notifyGallerySizeResolveFinished

### Apply
```bash
git pull --ff-only …/biltoo-2472.1-own-gallery-decode-book-7d823d8.bundle HEAD
```
