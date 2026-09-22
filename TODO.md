# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2326-print-thumbnailbar-include.**

Fix incomplete type: `mainwindow_print.cpp` needs `#include "thumbnailbar.h"`
for `selectedIndices()` in Export Images scope.

Requires **thumtoo-323**. Includes 2318–2325.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2326.1-print-thumbnailbar-include-999be36.bundle HEAD
```

Next: **2327** — session reorder UI.

## Backlog
- Session reorder UI
