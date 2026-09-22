# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2338-open-selection-no-private-seed-api.**

Fix: do not call private DisplayPipelineController::markAppearanceSeedAttempted
from MainWindow. Transferred appearance already wins over path-XDG seed via
hasContentAppearance in applyStoredContentAppearanceSeed.

Requires **thumtoo-323**. Includes 2318–2337.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2338.1-open-selection-no-private-seed-api-2f201f6.bundle HEAD
```

Next: **2339**.

## Backlog
- Gallery-canvas drag reorder (drag tiles on the packed canvas itself)
