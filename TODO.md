# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2784.1-annotation-format-ecs` (base `a989daf`).

### 2784.1 — Annotation format, persistence, data model
- formatVersion 2 envelope (`{ formatVersion, pages }`); string kind/blend names;
  decimal-string ids (no double precision risk); legacy array + int kinds still load
- Sticky body field `text` (highlight keeps `textSnippet`); load promotes legacy
- `AnnotationSession::updateObject` + `AnnotationReplaceCommand` (in-place edit, keeps z-order)
- Project load always applies annotations (empty clears prior session markup — was a leak)
- Dedicated `tryMouseDoubleClick` for sticky edit; ViewShellChrome routes it (not via press)
- Docs: ANNOTATION_OVERLAY status + on-disk format

### Prior
- 2783.1 fix annotation compile (brace + ImageItem include)
- 2782.2 sticky double-click edit

### Apply
```bash
git pull --ff-only …/biltoo-2784.1-annotation-format-ecs-a989daf.bundle HEAD
```

### Still open (not in this tip)
- Multi-select / shift-select; move/resize shapes after place
- Persist colour/width tool prefs; show/hide layer in UI
- Content-hash binding of pageBounds (detect page size change)
- AnnotationPainter split from controller (paint collaborator)
- PDF /Annot export
