# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2335-open-selection-sessionopen-include.**

Fix: `loadSessionSnapshots` needs `#include "sessionopen.h"` (SessionOpen
lives outside mainwindow_includes).

Requires **thumtoo-323**. Includes 2318–2334.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2335.1-open-selection-sessionopen-include-2f201f6.bundle HEAD
```

Next: **2336**.

## Backlog
- Gallery-canvas drag reorder (drag tiles on the packed canvas itself)
