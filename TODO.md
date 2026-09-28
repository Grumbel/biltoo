# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2806.4-tool-cursor-stick` (base `2085c07`).

### 2806.4
- Cursor: apply to view **and** viewport (sticky viewport cursor was ignoring
  restoreToolCursor set only on the view)
- Canvas tool group ExclusiveOptional; annotation activation unchecks all
  Select/Pan/Zoom so a re-click on Select can dismiss annotation
- updateWorkspaceActionVisibility no longer re-checks Select under annotation
- Text/attention/edge/zoom use applyToolCursor / restoreToolCursor

### 2806.3
- Mutual exclusion: Select/Pan/Zoom clears annotation tools

### 2806.2 / 2806.1
- qRound hotspot; custom coloured tool cursors

### Apply
```bash
git pull --ff-only …/biltoo-2806.4-tool-cursor-stick-2085c07.bundle HEAD
```
