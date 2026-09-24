# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2419-sole-pipeline-friend** (base `d80d461`).

### Phase 5 ownership
- Sole `ImageItem` friend for pixels: **DisplayPipelineController**
- Crop intrinsic fallback via `ImageView::setItemIntrinsicSize`
- GalleryLayout uses public host surface (no friends)

### Next
4. Dual ImageView (0.3) — large feature, separate track

### Apply
```bash
git pull --ff-only …/biltoo-2419.1-sole-pipeline-friend-d80d461.bundle HEAD
```
