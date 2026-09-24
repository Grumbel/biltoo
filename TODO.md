# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2434.1-pipeline-no-view-install-detour** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Intrinsic + bake + rematerialize on pipeline
- **Pipeline self-calls hostSetPreviewImage / rematerializeItemContent (2434)**
  - no m_view→setItemPreviewImage / rematerialize detour

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- item->setIntrinsicSize / setPreviewImage: only via pipeline host methods
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers (capture/undo)
- mode controllers clear pixels via ImageView host (acceptable)

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2434.1-pipeline-no-view-install-detour-d80d461.bundle HEAD
```
