# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2615.1-restore-edgezone-sticky-cancelzoom** (base `7d823d8`).

### This tip
Restore thin ImageView bodies for host-surface declarations left without
definitions after the transform peel:

- `edgeZoneAt` → `m_image.edgeZoneAt` (+ EdgeZone policy map)
- `restoreStickyPanAnchor` → `m_image.restoreStickyPanAnchor`
- `cancelZoomRegion` → `m_image.cancelZoomRegion`

### Apply
```bash
git pull --ff-only …/biltoo-2615.1-restore-edgezone-sticky-cancelzoom-7d823d8.bundle HEAD
```
