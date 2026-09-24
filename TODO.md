# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2421.3-verify-ownership** (base `d80d461`).

### Phase 5 ownership — complete for friend/pixel boundary
- Sole ImageItem pixel friend: DisplayPipelineController
- Residual ImageView `clearDecodedPixels` call sites fixed (appearance_commit, canvas)
- Host surface public; crop/gallery no longer friends

### Verification (2026-09-24, agent — three passes)
- Static: only `friend class DisplayPipelineController` on ImageItem (private)
- Static: every `item->clearDecodedPixels` / `setSourceImage*` / `setIntrinsicSize` /
  `setPreviewImage` / `setPath` / `attachTileLodBag` / `detachTileLodBag` call is in
  `displaypipelinecontroller.cpp`
- Static: ImageView hosts null-check then forward to pipeline hosts
- Static: crop/gallery/workspace/slideshow use view hosts only
- Finding (not a regression): `ImageItem::setSourceImage` is defined but currently
  **uncalled** from any translation unit — pipeline uses `setSourceImageReady` /
  `setPreviewImage` only. Left in place (no silent removal).
- Docs: `IMAGEVIEW_ITEM_OWNERSHIP.md` matches code
- Full `nix build` / characterization **not run** (no Nix store)

### Next (product)
- Dual ImageView (0.3) — separate track

### Apply
```bash
git pull --ff-only …/biltoo-2421.3-verify-ownership-d80d461.bundle HEAD
```
