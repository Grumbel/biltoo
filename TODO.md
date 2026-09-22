# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2339-gallery-canvas-drag-reorder.**

Gallery tiles are draggable for session reorder: press keeps multi-select,
past drag threshold starts QDrag with paths + session-ids; drop uses existing
handleGalleryDrop internal reorder (same as filmstrip → Gallery).

Requires **thumtoo-323**. Includes 2318–2338.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2339.1-gallery-canvas-drag-reorder-2f201f6.bundle HEAD
```

Next: **2340**.

## Backlog
- (none for reorder — filmstrip + Gallery canvas + dialog covered)
