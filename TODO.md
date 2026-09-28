# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2782.2-sticky-edit-verify` (base `b8a0cf3`).

### Verified
- Annotation tools: freehand/text highlight, pen, eraser, select, rect/ellipse/line, sticky
- Software-raster viewport + Multiply
- Project JSON annotations save/load
- Export Page with Annotations (PNG)
- Icons in qrc; exclusive tool group; CMake sources

### 2782.2
- Double-click sticky (Select tool) → edit text (undo macro)
- Double-click routed via ViewShellChrome::handleMouseDoubleClick

### Apply
```bash
git pull --ff-only …/biltoo-2782.2-sticky-edit-verify-b8a0cf3.bundle HEAD
```
