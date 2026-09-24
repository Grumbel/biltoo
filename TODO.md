# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2426.1-bake-rematerialize-pixels** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Layout + rematerialize + async on pipeline (2423–2424)
- **Bake pixel path → `rematerializeItemContent` (2426)**
  - Rotate/flip want composition, ItemWorld contentBake, undo stay on ImageView
  - Soft/async install no longer duplicated in bake
  - Cold-cache disk soft residual only when still no display pixels

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- bake has no tryRematerializeFromHost; uses rematerializeItemContent
- Full nix build not run in this sandbox

### Next
- Optional: move bake orchestration (undo/setContentBake) onto pipeline
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2426.1-bake-rematerialize-pixels-d80d461.bundle HEAD
```
