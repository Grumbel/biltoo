# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2433.2-intrinsic-host-only** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Bake orchestration on pipeline (2432)
- **`hostSetIntrinsicSize` owns Gallery LQIP guard + all intrinsic writes (2433)**
  - applyContentLayoutSize and path-change layout use hostSetIntrinsicSize
  - ImageView::setItemIntrinsicSize thin forward

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- `item->setIntrinsicSize`: only inside hostSetIntrinsicSize
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers (capture/undo)

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2433.2-intrinsic-host-only-d80d461.bundle HEAD
```
