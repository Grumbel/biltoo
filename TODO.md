# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2421.2-verify-ownership** (base `d80d461`).

### Phase 5 ownership — complete for friend/pixel boundary
- Sole ImageItem pixel friend: DisplayPipelineController
- Residual ImageView `clearDecodedPixels` call sites fixed (appearance_commit, canvas)
- Host surface public; crop/gallery no longer friends

### Verification (2026-09-24, agent — two passes)
- Static: only `friend class DisplayPipelineController` on ImageItem (private section)
- Static: all `item->clearDecodedPixels` / `setSourceImage*` / `setIntrinsicSize` /
  `setPreviewImage` call sites are inside `displaypipelinecontroller.cpp`
- Static: ImageView routes via `clearItemDecodedPixels` → `hostClearDecodedPixels` and
  `setItemIntrinsicSize` → `hostSetIntrinsicSize`; crop/gallery/workspace/slideshow use view hosts
- Static: residual sites in `imageview_appearance_commit.cpp` and `imageview_canvas.cpp` use hosts
- Static: slideshow `setSourceImage` hits dwell phase types, not ImageItem
- Docs: `docs/IMAGEVIEW_ITEM_OWNERSHIP.md` matches code (extraction backlog 1–3b Done)
- Full `nix build` / characterization **not run** (no Nix store in this sandbox)

### Next (product)
- Dual ImageView (0.3) — separate track

### Apply
```bash
git pull --ff-only …/biltoo-2421.2-verify-ownership-d80d461.bundle HEAD
```
