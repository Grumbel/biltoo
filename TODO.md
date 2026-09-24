# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2424.1-rematerialize-pipeline** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Dead `setSourceImage` removed (2422)
- `applyContentLayoutSize` on pipeline (2423)
- **`rematerializeItemContent` + async host rematerialize on pipeline (2424)**
  - `tryRematerializeFromHost` private on pipeline
  - ImageView / crop / workspace keep thin public forwards
  - Pipeline self-calls scheduleAsync (no detour through view)

### Next
- Dual ImageView (0.3) — product track
- Phase 6: header closure → bake / paint collaborators

### Apply
```bash
git pull --ff-only …/biltoo-2424.1-rematerialize-pipeline-d80d461.bundle HEAD
```
