# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2416-item-ownership-doc** (base `d80d461`).

### Phase 5 ownership — slice 1 started
- **Doc:** `docs/IMAGEVIEW_ITEM_OWNERSHIP.md` (graph + backlog)
- Linked from `MODE_OWNERSHIP.md`, `AGENTS.md`, `ImageItem` class comment
- Hygiene: ImageView `*.cpp` clear/intrinsic go through `clearItemDecodedPixels` /
  `setItemIntrinsicSize` (except the wrappers themselves in `imageview_appearance.cpp`)
- Pipeline remains authorized friend for install paths

### Next slices
2. Move `attachDisplaySample` body into `DisplayPipelineController`
3. Narrow / remove `friend class ImageView` on `ImageItem`
4. Dual ImageView (0.3) on top of thin façade

### Apply
```bash
git pull --ff-only …/biltoo-2416.1-item-ownership-doc-d80d461.bundle HEAD
```

Prior: 2415 slideshow tile cover; 2414 pan-zoom factor.
