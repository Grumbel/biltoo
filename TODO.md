# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2443.2-drop-rematerialize-forwards-tu** (base `d80d461`).

### Phase 5 / Tier 0 cleanup
- **Deleted `src/imageview_rematerialize.cpp`** and CMake entry
- Removed dead private ImageView forwards (no callers):
  - attachDisplaySample, applyContentLayoutSize, scheduleAsyncHostRematerialize
  - rematerializeItemContent, rematerializeGalleryItemFromStore, clearStale…
  - clearItemDecodedPixels, setItemIntrinsicSize, setItemPreviewImage
- All pixel/layout install goes to DisplayPipelineController only

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- No ImageView rematerialize/attach/clear/preview/intrinsic symbols
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers public for pipeline
- rotateContentByQuarterTurns public

### Next
- Phase 6 Tier 0 remainder / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2443.2-drop-rematerialize-forwards-tu-d80d461.bundle HEAD
```
