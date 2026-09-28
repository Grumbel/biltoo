# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2790.1-annotation-panel` (base `a989daf`).

### 2790.1 — Annotations panel
- AnnotationPanel dock: colour presets, custom colour, stroke width slider/spin,
  layer visibility checkbox
- Panels menu + dock toggle; sync with tool switch and Image colour/width menus
- Also fixed pre-existing brace nesting around crop/ocr panel toggle actions

### Prior
- 2789.1 selectAtPagePoint -Wduplicated-cond
- 2788–2783 annotation tools / format / compile

### Apply
```bash
git pull --ff-only …/biltoo-2790.1-annotation-panel-a989daf.bundle HEAD
```

### Still open
- Validate sourceKey on project load
- AnnotationPainter split
- PDF /Annot export
