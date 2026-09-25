# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2653.1-batch-targets-beyond-live` (base `9740316`).

### Done
- Shared ContentUndoMacro + crop panel polish (2652).
- **2653.1 Targets beyond live items**
  - `BatchTargets::resolve` — Current / Selection (canvas ∪ filmstrip) /
    Session index range.
  - Filmstrip `selectedSessionIds()` / `selectedSessionIndices()`.
  - Crop apply/reset via `applyCropRecipeToBatch` / `resetCropOnBatch`:
    live tiles + **ItemWorld-only** for virtual slots.
  - `ImageView::pushSessionContentCommand` undoes non-live writes.
  - Crop panel **Targets** combo + index range spinboxes.

### Next
1. Wire colour multi-apply through the same BatchTargets resolver.
2. Template / even-odd / stack (later).

### Apply
```bash
git pull --ff-only …/biltoo-2653.1-batch-targets-beyond-live-9740316.bundle HEAD
```
