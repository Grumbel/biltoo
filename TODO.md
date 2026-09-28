# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2789.1-fix-select-dup-cond` (base `a989daf`).

### 2789.1
- Fix -Wduplicated-cond in selectAtPagePoint (dead else branch)

### Prior
- 2788.1 line endpoints + sourceKey
- 2787.1 corner resize
- 2786–2785 select move / multi-select / prefs
- 2784 format v2
- 2783 compile fix

### Apply
```bash
git pull --ff-only …/biltoo-2789.1-fix-select-dup-cond-a989daf.bundle HEAD
```

### Still open
- Validate sourceKey vs current path on project load
- AnnotationPainter split
- PDF /Annot export
- Multi-quad uniform scale
