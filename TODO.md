# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2793.1-pdf-writeback-defer-doc` (base `a989daf`).

### 2793.1
- Document deferred PDF source write-back / `/Annot` in `docs/PDF_SOURCE_WRITEBACK.md`
- Not implementing structural PDF edits this pass (keep Qt raster export + project JSON)

### Prior tip behaviour
- 2792.1 AnnotationPainter extraction
- 2791–2783 annotation tools, panel, format, compile

### Apply
```bash
git pull --ff-only …/biltoo-2793.1-pdf-writeback-defer-doc-a989daf.bundle HEAD
```

### Still open (annotation-related)
- (none forced) — optional polish only

### Deferred (see docs/PDF_SOURCE_WRITEBACK.md)
- PDF `/Annot` and broader non-destructive write-back into source PDFs
