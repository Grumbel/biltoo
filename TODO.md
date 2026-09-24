# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2427.1-rematerialize-cold-disk** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Layout + rematerialize + async on pipeline
- Bake pixels → rematerializeItemContent (2426)
- **Cold-cache disk soft inside pipeline rematerialize (2427)**
  - bakeItemRotate90/Flip no longer call ImageLoader
  - bake is want composition + ItemWorld + undo only

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- bake has no loadThumbnail / ImageCache
- Full nix build not run in this sandbox

### Next
- Optional: move bake orchestration (undo/setContentBake) onto pipeline
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2427.1-rematerialize-cold-disk-d80d461.bundle HEAD
```
