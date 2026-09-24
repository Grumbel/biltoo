# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2425.1-bake-uses-pipeline-try** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Dead `setSourceImage` removed (2422)
- `applyContentLayoutSize` on pipeline (2423)
- Rematerialize + async on pipeline (2424)
- **Bake/crop restore use pipeline `tryRematerializeFromHost` (2425)**
  - Made try public (bake + crop appearance restore)
  - Dropped dead ImageView private try/finish decls (would not link after 2424)
  - `bakeItemRotate90` / `bakeItemFlip` still on ImageView for undo/ItemWorld; pixel path is pipeline

### Verification
- Friend: only DisplayPipelineController on ImageItem
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- tryRematerializeFromHost: only pipeline impl; callers use m_displayPipeline
- Full nix build not run in this sandbox

### Next
- Move bakeItemRotate90/Flip orchestration onto pipeline (or leave with undo on view)
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2425.1-bake-uses-pipeline-try-d80d461.bundle HEAD
```
