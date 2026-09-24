# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2433.1-intrinsic-lqip-pipeline** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Bake orchestration on pipeline (2432)
- **`hostSetIntrinsicSize` owns Gallery LQIP guard (2433)**
  - applyContentLayoutSize uses hostSetIntrinsicSize directly
  - ImageView::setItemIntrinsicSize thin forward

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- setIntrinsicSize: only via hostSetIntrinsicSize in pipeline
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers (capture/undo)

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2433.1-intrinsic-lqip-pipeline-d80d461.bundle HEAD
```
