# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2455.1-stage-2c0-active-host-shared-itemworld** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake: CLOSED**
- **Dual ImageView Stage 0–2b: LANDED** — pipeline bound only to DisplayPipelineHost
- **Stage 2c.0: LANDED** — `setActiveHost` + `bindSharedItemWorld` (no dual UI yet)

### Stage 2c.0 summary
- `DisplayPipelineController::setActiveHost(DisplayPipelineHost *)`
- `ImageView::bindSharedItemWorld(ItemWorld *)` / `sharedItemWorld()`
- Docs: PreferCache rules sketch for two panes (see IMAGEVIEW_ITEM_OWNERSHIP.md)

### Verification (static)
| Check | Result |
|-------|--------|
| Pipeline `m_view` / `view()` | still removed |
| setActiveHost | GUI-thread, non-null host |
| itemWorld() | shared pointer or owned |
| Dual-pane shell | **not started** |
| Runtime / nix build | not run in sandbox |

### Next
- Stage 2c: dual-pane shell (two ImageViews, shared ItemWorld, focus → setActiveHost)
- QTimer lifetime policy for host switch
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2455.1-stage-2c0-active-host-shared-itemworld-7d823d8.bundle HEAD
```
