# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2341.2-gallery-drag-no-qpointer.**

Fix: ImageItem is not QObject — Gallery drag uses raw ImageItem* (not
QPointer). Dropped unused QPointer include.

Requires **thumtoo-323**. Includes 2318–2340.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2341.2-gallery-drag-no-qpointer-2f201f6.bundle HEAD
```

Next: **2342**.

## Backlog
- (reorder covered: filmstrip, Gallery canvas, dialog)
