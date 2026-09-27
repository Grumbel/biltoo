# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.6-gallery-underlay-orient` (base `bcbb97e`).

### 2714.6 — Gallery underlay respects orientation
- EXIF embedded thumbs are often un-autorotated while Store size is upright —
  transpose underlay when aspect disagrees with definitive size.
- `tryInstallGalleryUnderlay`: never `hostSetPreviewImage(raw)`; always
  `installDisplayPixels` + `rematerializeGalleryItemFromStore` so session
  rotate/flip is applied. Re-install when live sample aspect ≠ layout.
- Virtual placeholders: same upright + session materialize before paint.

### Prior (included)
2714.5 overlay scroll · 2714.4 viewport · 2714.3–1 F5/tiles

### Apply
```bash
git pull --ff-only …/biltoo-2714.6-gallery-underlay-orient-bcbb97e.bundle HEAD
```
