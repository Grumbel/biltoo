# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2432.1-bake-orchestration-pipeline** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- **`bakeItemRotate90` / `bakeItemFlip` on pipeline (2432)**
  - Host: captureContentBakeBeforeState, appearanceCropMapForEdit,
    persistDurableContentAppearance, pushItemContentCommand (public host ops)
  - ImageView thin forwards only

### Verification
- Friend: only DisplayPipelineController
- Private pixel/path/tile mutators: only displaypipelinecontroller.cpp
- imageview_bake.cpp: forwards only (~17 lines)
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers (undo stack / freeze) — intentional view collaboration

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2432.1-bake-orchestration-pipeline-d80d461.bundle HEAD
```
