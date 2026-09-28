# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2787.1-annot-corner-resize` (base `a989daf`).

### 2787.1 — Corner resize (Select tool)
- Single selection with one quad (rect, ellipse, sticky, single highlight): corner
  handles; drag resizes with opposite corner fixed; min size 2×2 page units
- Undo via AnnotationReplaceCommand; Escape / tool change cancels
- Multi-select and stroke-only objects still move-only

### Prior
- 2786.1 select drag-move
- 2785.1 multi-select, layer visibility, prefs
- 2784.1 format v2 / ReplaceCommand
- 2783.1 compile fix

### Apply
```bash
git pull --ff-only …/biltoo-2787.1-annot-corner-resize-a989daf.bundle HEAD
```

### Still open
- Content-hash binding of pageBounds
- AnnotationPainter split from controller
- PDF /Annot export
- Line endpoint resize; multi-quad scale
