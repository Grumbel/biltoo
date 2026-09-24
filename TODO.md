# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2435.1-controllers-pipeline-host** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- **Mode controllers use hostDisplayPipeline for pixel/layout ops (2435)**
  - clear → hostClearDecodedPixels
  - attach / rematerialize / scheduleAsync / intrinsic / layout → pipeline

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- Controllers: no m_view clearItemDecodedPixels / attachDisplaySample / rematerialize
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers (capture/undo)
- ImageView thin forwards remain for public API

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2435.1-controllers-pipeline-host-d80d461.bundle HEAD
```
