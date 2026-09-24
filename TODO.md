# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2442.1-drop-imageview-bake-tu** (base `d80d461`).

### Phase 6 Tier 0 (continued)
- **Removed ImageView bake TU (2442)**
  - Deleted `src/imageview_bake.cpp` and CMake entry
  - Removed private `bakeItemFlip` / `bakeItemRotate90` ImageView symbols
  - Sole bake entry points: `DisplayPipelineController::bakeItemRotate90` / `bakeItemFlip`
  - Public: `rotateContentByQuarterTurns` (pipeline bake + Image-mode framing)

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- No ImageView bake* symbols
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers public for pipeline
- rotateContentByQuarterTurns public

### Next
- More Phase 6 Tier 0 / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2442.1-drop-imageview-bake-tu-d80d461.bundle HEAD
```
