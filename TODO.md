# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2791.1-annot-multiquad-sourcekey-check` (base `a989daf`).

### 2791.1
- Multi-quad corner resize: scale all quads through union box (text highlights)
- Project load: status bar warning when annotation sourceKey ≠ session path
- Selecting an annotation tool opens the Annotations panel

### Prior
- 2790.1 Annotations panel
- 2789–2783 format, select, move, resize, compile

### Apply
```bash
git pull --ff-only …/biltoo-2791.1-annot-multiquad-sourcekey-check-a989daf.bundle HEAD
```

### Still open
- AnnotationPainter split from controller
- PDF /Annot export
- Auto-scale geometry when pageBounds size changes vs stored
