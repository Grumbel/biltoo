# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2430.1-identity-reset-pixels** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Layout, rematerialize, async, cold disk, gallery store, interactive grade SoftPreview
- **`reinstallModePixelsAfterIdentityReset` (2430)** — Reset Content Appearance soft/full

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- appearance_commit identity reinstall: pipeline only (no ImageLoader in TU)
- Full nix build not run in this sandbox

### Residual on ImageView
- bakeItemRotate90/Flip: want + ItemWorld contentBake + undo
- clearStaleAppliedFingerprintIfNeeded
- interactive grade live-grade fast path + filmstrip emit

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2430.1-identity-reset-pixels-d80d461.bundle HEAD
```
