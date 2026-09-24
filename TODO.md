# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2619.1-peel-load-schedule-modeflags** (base `7d823d8`).

### This tip
Peeled thin load/mode helpers off ImageView:

| Was on ImageView | Now |
|------------------|-----|
| `scheduleReplaceLoad` | `hostDisplayPipeline().scheduleImageLoad(..., LoadReplace)` |
| `scheduleRestoreLoad` | `… LoadRestore` |
| `invalidateSessionLoads` | `hostDisplayPipeline().invalidateSessionLoads()` |
| `applyModeFlagsToLiveItems` | Removed (call sites were no-ops on empty canvas) |

Routers stay domain-split. Header continues to shrink.

### Apply
```bash
git pull --ff-only …/biltoo-2619.1-peel-load-schedule-modeflags-7d823d8.bundle HEAD
```
