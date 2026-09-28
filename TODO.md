# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2786.1-annot-select-move` (base `a989daf`).

### 2786.1 — Select drag-move
- Drag selected annotation(s) in page space (Select tool, plain click on selection)
- Live preview from baseline + delta; commit via AnnotationMoveCommand (undo/redo)
- Escape cancels in-progress move; tool change cancels and restores baseline
- Shift/Ctrl multi-select presses do not start a move

### Prior
- 2785.1 multi-select, layer visibility, colour/width prefs
- 2784.1 formatVersion 2, ReplaceCommand, sticky text, load clear
- 2783.1 compile fix

### Apply
```bash
git pull --ff-only …/biltoo-2786.1-annot-select-move-a989daf.bundle HEAD
```

### Still open
- Resize shapes after place (handles)
- Content-hash binding of pageBounds
- AnnotationPainter split from controller
- PDF /Annot export
