# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2452.1-display-pipeline-host-stage2a** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake: CLOSED**
- **Dual ImageView Stage 0–2a: LANDED** — pipeline decoupled from ImageView* for async/tiles

### Stage 2a summary
- Jobs: `QPointer<QObject> life` + `DisplayPipelineController*` (no ImageView QPointer)
- TileLoadCoordinator: pipeline-bound; host via `pipe->host()`
- Timers / singleShot: `hostObject()` parent
- Worker lambdas: `life` + `pipe` (no hostDisplayPipeline hop)
- `m_view` only for ctor / `view()` API

### Verification (static)
| Check | Result |
|-------|--------|
| ImageItem friends | sole DisplayPipelineController |
| Pixel mutators outside pipeline | none |
| Pipeline `m_view->` (non-comment) | **0** |
| `QPointer<ImageView>` in display/ | **0** |
| Host pure virtuals | ~105 (+ viewTransform) |
| Runtime / nix build | not run |

### Next
- Stage 2b: active-host switching; dual-pane shell; optional drop view()
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2452.1-display-pipeline-host-stage2a-7d823d8.bundle HEAD
```
