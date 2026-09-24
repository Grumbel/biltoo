# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2420-fix-residual-clear** (base `d80d461`).

### Phase 5 ownership — complete for friend/pixel boundary
- Sole ImageItem pixel friend: DisplayPipelineController
- Residual ImageView `clearDecodedPixels` call sites fixed (appearance_commit, canvas)
- Host surface public; crop/gallery no longer friends

### Verification (2026-09-24, agent session)
- Static: only `friend class DisplayPipelineController` on ImageItem
- Static: all `item->clearDecodedPixels` / `setSourceImage*` / `setIntrinsicSize` call sites are inside `displaypipelinecontroller.cpp`
- Static: ImageView routes via `clearItemDecodedPixels` → `hostClearDecodedPixels`; crop/gallery/workspace use view hosts only
- Static: residual sites in `imageview_appearance_commit.cpp` and `imageview_canvas.cpp` use `clearItemDecodedPixels` (not private mutator)
- Docs: `docs/IMAGEVIEW_ITEM_OWNERSHIP.md` matches code (extraction backlog 1–3b marked Done)
- Full `nix build` / characterization **not run** in this sandbox (no Nix store)

### Next (product)
- Dual ImageView (0.3) — separate track

### Apply
```bash
git pull --ff-only …/biltoo-2420.1-fix-residual-clear-d80d461.bundle HEAD
```
