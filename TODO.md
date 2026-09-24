# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2420-fix-residual-clear** (base `d80d461`).

### Phase 5 ownership — complete for friend/pixel boundary
- Sole ImageItem pixel friend: DisplayPipelineController
- Residual ImageView `clearDecodedPixels` call sites fixed (appearance_commit, canvas)
- Host surface public; crop/gallery no longer friends

### Next (product)
- Dual ImageView (0.3) — separate track

### Apply
```bash
git pull --ff-only …/biltoo-2420.1-fix-residual-clear-d80d461.bundle HEAD
```
