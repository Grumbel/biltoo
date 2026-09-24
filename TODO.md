# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2438.1-privatize-setItemIntrinsicSize** (base `d80d461`).

### Phase 6 Tier 0 (continued)
- Pipeline-forward methods privatized (2437)
- **`setItemIntrinsicSize` private (2438)**
  - Sole writer: `DisplayPipelineController::hostSetIntrinsicSize`
  - Probe settle path uses pipeline host directly

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- `item->setIntrinsicSize`: only inside hostSetIntrinsicSize
- setItemIntrinsicSize not in public imageview.h
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers public for pipeline (capture/undo/persist)

### Next
- More Phase 6 Tier 0 / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2438.1-privatize-setItemIntrinsicSize-d80d461.bundle HEAD
```
