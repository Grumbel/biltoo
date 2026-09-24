# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2429.1-interactive-grade-pipeline** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Layout, rematerialize, async, cold disk, gallery store rematerialize on pipeline
- Bake pixels via rematerialize; orchestration on ImageView
- **`installInteractiveSoftPreview` for color-grade drag (2429)**

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- applyInteractiveColorGrade soft path: pipeline installInteractiveSoftPreview
- Full nix build not run in this sandbox

### Residual on ImageView
- bakeItemRotate90/Flip: want + ItemWorld contentBake + undo
- clearStaleAppliedFingerprintIfNeeded
- interactive grade: live-grade fast path + filmstrip emit (policy)

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2429.1-interactive-grade-pipeline-d80d461.bundle HEAD
```
