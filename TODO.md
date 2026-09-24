# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2428.1-gallery-rematerialize-pipeline** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Layout, rematerialize, async, cold disk soft on pipeline
- Bake pixels via rematerialize; bake orchestration stays on ImageView
- **`rematerializeGalleryItemFromStore` on pipeline (2428)**

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- Gallery store rematerialize: pipeline impl + ImageView forward
- Full nix build not run in this sandbox

### Residual on ImageView
- bakeItemRotate90/Flip: want + ItemWorld contentBake + undo
- clearStaleAppliedFingerprintIfNeeded (fingerprint meta, not pixels)
- interactive color-grade soft drag (SoftPreview while dragging)

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2428.1-gallery-rematerialize-pipeline-d80d461.bundle HEAD
```
