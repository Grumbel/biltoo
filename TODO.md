# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2792.1-annotation-painter` (base `a989daf`).

### 2792.1 — AnnotationPainter extraction
- New `AnnotationPainter`: page↔display↔scene mapping + paint of objects/page/chrome
- Controller keeps tools, input, undo, draft rubber-band; paintOverlay delegates
- Fix `unionOfQuads` scope (file-level helpers before first use)

### Prior
- 2791 multi-quad resize / sourceKey check / panel auto-open
- 2790 Annotations panel
- 2789–2783 tools, format, compile

### Apply
```bash
git pull --ff-only …/biltoo-2792.1-annotation-painter-a989daf.bundle HEAD
```

### Still open
- PDF /Annot export
