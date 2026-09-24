# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2423.1-layout-size-pipeline** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Dead `setSourceImage` removed (2422)
- **`applyContentLayoutSize` owned by DisplayPipelineController**
  - ImageView one-line forward for crop/bake hosts
  - `attachDisplaySample` calls pipeline layout (no double apply)
  - Redundant post-attach layout removed (rematerialize + crop draft enter)

### Next
- Dual ImageView (0.3) — product track
- Phase 6: header closure → rematerialize/bake/paint collaborators

### Apply
```bash
git pull --ff-only …/biltoo-2423.1-layout-size-pipeline-d80d461.bundle HEAD
```
