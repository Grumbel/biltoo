# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2417-attach-display-pipeline** (base `d80d461`).

### Phase 5 ownership — slice 2
- `DisplayPipelineController::attachDisplaySample` owns setPreview / setSourceReady
  + layout/meta sync (calls view host for layout/color)
- `ImageView::attachDisplaySample` is a one-line forward
- Doc backlog updated in IMAGEVIEW_ITEM_OWNERSHIP.md

### Next
3. Narrow / remove `friend class ImageView` on ImageItem
4. Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2417.1-attach-display-pipeline-d80d461.bundle HEAD
```
