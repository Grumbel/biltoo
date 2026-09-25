# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2703.2-workspace-delete-bsp-uaf` (base `6c3e877`).

### 2703.2 — SIGSEGV deleting Workspace item (BSP climbTree)
Paint during `processDirtyItems` walked a stale BSP leaf after
`removeItem`+`delete`. Batch Delete kept updates disabled but re-enabled
paint without rebuilding the index; `updateSceneRect` mid-destroy also
walked BSP.

Fix:
- Hide item before removeItem
- Skip `updateSceneRect` inside destroy while updates disabled
- After batch delete / clearLiveCanvas: toggle itemIndexMethod to rebuild BSP
- Single updateSceneRect after batch

### 2703.1 — size book vs tile-native mismatch

### Apply
```bash
git pull --ff-only …/biltoo-2703.2-workspace-delete-bsp-uaf-6c3e877.bundle HEAD
```
