# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2773.1-annot-text-highlight-undo` (base `b8a0cf3`).

### Stack
… 2772.1 freehand → **2773.1** text-snapped highlight + undo + colours

### 2773.1
- Text Highlighter tool: rubber-band → intersecting text regions → Multiply quads
- Freehand: RDP simplify; commits via QUndoStack (Ctrl+Z)
- Highlight Colour menu (yellow/green/cyan/pink/orange)
- Tools strip + Image menu

### Try
1. Image mode, OCR/native text layer present
2. **Text Highlighter** → drag over text
3. **Freehand Highlighter** → stroke; Undo
4. Save `.biltoo` — both kinds persist

### Next
- Width control, eraser/select
- Pen (SourceOver)
- GL Multiply visual check on dark text

### Apply
```bash
git pull --ff-only …/biltoo-2773.1-annot-text-highlight-undo-b8a0cf3.bundle HEAD
```
