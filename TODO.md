# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2636.1-gallery-scene-expand-center** (base `b867c6e`).

### This tip — off-centre Gallery with AlignCenter again
After restoring AlignCenter for zoom-out, a pack measured under AlwaysOn (or
otherwise smaller than the live client) was floated by AlignCenter → phantom
scrollbar margins / off-centre overview.

Fix: after bar policy restore, expand sceneRect to at least the live viewport
size, centred on the pack bounds. AlignCenter then fills the view; content
stays geometrically centred. Zoom-out still uses AlignCenter.

### Apply
```bash
git pull --ff-only …/biltoo-2636.1-gallery-scene-expand-center-b867c6e.bundle HEAD
```
