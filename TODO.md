# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2806.3-tool-mutex-cursor` (base `2085c07`).

### 2806.3
- Mutual exclusion: Select/Pan/Zoom clears annotation tools; annotation
  activation forces canvas tool to Select and cancels zoom region
- restoreToolCursor / setTool / edge-nav cursor respect active annotation tool

### 2806.2
- Fix: qRound for DPR hotspot (Qt 6.11 qBound is clamp-only)

### 2806.1
- Custom coloured 32×32 tool cursors (SVG → QPixmap via ToolCursors)
- Crosshair hotspot for precision tools; tool badge bottom-right
- Canvas + annotation + crop outside + attention + text + workspace crosshair
- Resize handles stay stock Qt size cursors

### Apply
```bash
git pull --ff-only …/biltoo-2806.3-tool-mutex-cursor-2085c07.bundle HEAD
```
