# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2788.1-annot-endpoint-sourcekey` (base `a989daf`).

### 2788.1 — Line endpoints + page sourceKey
- ShapeLine: endpoint handles when singly selected; drag moves that point (undo)
- Page.sourceKey (path) recorded on commit; persisted in formatVersion 2 JSON
  for later remap / size-change detection

### Prior
- 2787.1 corner resize
- 2786.1 select drag-move
- 2785.1 multi-select / visibility / prefs
- 2784.1 format v2
- 2783.1 compile fix

### Apply
```bash
git pull --ff-only …/biltoo-2788.1-annot-endpoint-sourcekey-a989daf.bundle HEAD
```

### Still open
- Validate sourceKey vs current path on project load (warn / scale)
- AnnotationPainter split from controller
- PDF /Annot export
- Multi-quad uniform scale
