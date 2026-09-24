# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2451.1-display-pipeline-host-stage1b** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake: CLOSED**
- **Dual ImageView Stage 0–1b: LANDED** — DisplayPipelineHost complete for pipeline

### Stage 1b summary
- Host covers residual long-tail (tr, load/place/pack, text/workspace bags,
  bake helpers, scene mapping, …)
- Pipeline: **0** non-comment `m_view->` call sites
- `m_view` kept for QPointer / QTimer parent / TileLoadCoordinator only
- Fixed self-call through view for loadGate()

### Verification (static)
| Check | Result |
|-------|--------|
| ImageItem friends | sole DisplayPipelineController |
| Pixel mutators outside pipeline | none |
| bake/rematerialize TUs | absent |
| Host pure virtuals | ~104; ImageView overrides present |
| Pipeline m_view-> (non-comment) | **0** |
| Runtime / nix build | not run |

### Next
- Stage 2: active-host switching; dual-pane shell; async guards via hostObject
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2451.1-display-pipeline-host-stage1b-7d823d8.bundle HEAD
```
