# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2620.1-peel-currenttool-sessionindex** (base `7d823d8`).

### This tip
| Peeled | Replacement |
|--------|-------------|
| `currentTool()` | `hostWorkspace().currentTool()` |
| `setItemSessionIndex` | `item->setSessionIndex(...)` |

Header ~509 lines. Domain routers kept split.

### Remaining intentional public API
Mode shell, status gather, selectedPaths/pendingDecodeCount, capture/freeze
appearance, bindShared*, setActiveMode, host surface.

### Apply
```bash
git pull --ff-only …/biltoo-2620.1-peel-currenttool-sessionindex-7d823d8.bundle HEAD
```
