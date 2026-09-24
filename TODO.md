# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2461.1-fix-dual-per-surface-pipeline** (base `7d823d8`).

### Dual compare goal
Side-by-side Image-mode compare of two session rows. Shared **ItemWorld**
(appearance). **Per-surface DisplayPipeline** so both panes can hold pixels at once.

### Why secondary was black
Shared pipeline + single `activeHost` rejected secondary installs when
`classicPath` / host still referred to the primary. OpenGL dual viewports were
a secondary suspicion; installs never reached the right scene.

### Fix 2461
- Dual secondary keeps its **own** DisplayPipelineController
- Still `bindSharedItemWorld` (+ shared size book via hostSizeBook)
- `setActiveHost` only when `hasSharedDisplayPipeline()`

### Next
- PreferCache coordination across two pipelines (same SessionImageId)
- Optional lock-step nav
- Confirm both panes show images on host

### Apply
```bash
git pull --ff-only …/biltoo-2461.1-fix-dual-per-surface-pipeline-7d823d8.bundle HEAD
```
