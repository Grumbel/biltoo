# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2431.1-clear-stale-fingerprint-pipeline** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Layout, rematerialize, async, cold disk, gallery store, interactive SoftPreview,
  identity-reset reinstall on pipeline
- **`clearStaleAppliedFingerprintIfNeeded` on pipeline (2431)**

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- clearStale: pipeline impl; ImageView + workspace keep host forward
- Full nix build not run in this sandbox

### Residual on ImageView
- bakeItemRotate90/Flip: want + ItemWorld contentBake + undo
- interactive grade live-grade fast path + filmstrip emit

### Next
- Phase 6 header closure / Dual ImageView (0.3)
- Optional: move bake orchestration onto pipeline (needs host for undo/push)

### Apply
```bash
git pull --ff-only …/biltoo-2431.1-clear-stale-fingerprint-pipeline-d80d461.bundle HEAD
```
