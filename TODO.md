# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2646.1-batch-orient-undo-macro` (base `bbfdf32`).

### Done
- Orient multi-apply already used `transformTargets()` (Gallery/Workspace selection).
- **Undo bag:** `ContentUndoMacro` groups flip/rotate content undos when N>1.
- **Reset content appearance:** before/after snapshot + `pushItemContentCommand`
  under the same macro (was not undoable before).

### Next (batch appearance)
1. Colour multi-apply to targets (AdjustmentsPanel).
2. Crop panel (manual / autocrop+threshold / margins / reset).
3. Template page + stack preview later.

### Apply
```bash
git pull --ff-only …/biltoo-2646.1-batch-orient-undo-macro-bbfdf32.bundle HEAD
```
