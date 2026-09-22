# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2331-filmstrip-drop-dragdrop-mode.**

Fix: filmstrip was `DragOnly`, so internal session-row drops never landed.
Use `DragDrop` + acceptDrops; keep `Static` movement (host owns order).
Map drop positions into the viewport for the insertion line / insert index.

Requires **thumtoo-323**. Includes 2318–2330.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2331.1-filmstrip-drop-dragdrop-mode-2f201f6.bundle HEAD
```

Next: **2332**.

## Backlog
- Gallery drag reorder
