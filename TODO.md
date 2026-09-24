# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2437.1-tier0-privatize-pipeline-forwards** (base `d80d461`).

### Phase 5 / Phase 6 Tier 0
- Sole ImageItem pixel friend: DisplayPipelineController
- Controllers + ImageView TUs call pipeline for pixel/layout
- **Privatized ImageView methods no longer needed on public host (2437):**
  - attachDisplaySample, applyContentLayoutSize, scheduleAsyncHostRematerialize
  - rematerializeItemContent, rematerializeGalleryItemFromStore
  - clearStaleAppliedFingerprintIfNeeded, clearItemDecodedPixels, setItemPreviewImage
  - still available as **private** forwards for any internal use
- Gallery/Workspace remaining hops → hostDisplayPipeline

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- Controllers: no ImageView pixel-forward methods (use pipeline)
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers (capture/undo)
- setItemIntrinsicSize still public (optional later privatize)

### Next
- Continue Phase 6 Tier 0 (more public→private) / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2437.1-tier0-privatize-pipeline-forwards-d80d461.bundle HEAD
```
