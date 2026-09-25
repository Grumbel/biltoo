# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2654.1-colour-batch-targets` (base `9740316`).

### Done
- Crop batch targets beyond live (2653).
- **2654.1 Colour multi-apply via BatchTargets**
  - `applyColorAdjustmentsToBatch` — live install + ItemWorld for virtual ids.
  - Adjustments panel **Targets** (Current / Selection+filmstrip / index range).
  - MainWindow resolves filmstrip selection into the batch list.

### Next
1. Template / even-odd / stack (later).
2. Orient (flip/rotate) through BatchTargets if filmstrip-only selection matters.

### Apply
```bash
git pull --ff-only …/biltoo-2654.1-colour-batch-targets-9740316.bundle HEAD
```
