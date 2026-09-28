# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2806.1-custom-tool-cursors` (base `2085c07`).

### 2806.1
- Custom coloured 32×32 tool cursors (SVG → QPixmap via ToolCursors)
- Crosshair hotspot for precision tools; tool badge bottom-right
- Canvas: Select / Pan open+closed / Zoom
- Annotation: all tools (pen, highlighter, text-hl, eraser, shapes, sticky, select)
- Crop outside, attention draw, text-layer select, workspace crosshair
- Resize handles stay stock Qt size cursors
- ToolPolicy::cursorFor now returns QCursor (pixmap with stock fallback)

### Apply
```bash
git pull --ff-only …/biltoo-2806.1-custom-tool-cursors-2085c07.bundle HEAD
```
