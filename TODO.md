# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2664.1-menu-document-thumbs-flat` (base `2e49220`).

### Stack
1. **2663.1** — Open Selection content snapshot (not pose-only durable)
2. **2664.1** — Menu: Image EPUB/PDF flat; View filmstrip actions flat; drop edge position menu

### This tip
- Image: EPUB Layout + PDF Embedded Images directly under Image (no Document submenu).
- View: Show Thumbnails / Hide labels / Crop to square after a separator (no Thumbnails submenu).
- Removed Thumbnails on Top/Bottom/Left/Right menu actions — dock via drag.
- `setThumbnailBarPosition` kept for restore / dock geometry.

### Apply
```bash
git pull --ff-only …/biltoo-2664.1-menu-document-thumbs-flat-2e49220.bundle HEAD
```
