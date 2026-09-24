# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2441.1-drop-bakeItemRotate90-forward** (base `d80d461`).

### Phase 6 Tier 0 (continued)
- bakeItemFlip private (2440)
- **Removed ImageView::bakeItemRotate90 (2441)**
  - Sole implementation: DisplayPipelineController::bakeItemRotate90
  - rotateContentByQuarterTurns → m_displayPipeline.bakeItemRotate90 + framing
  - ImageView::bakeItemFlip private forward remains (unused externally; keep for symmetry)

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- No ImageView::bakeItemRotate90 symbol
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers public for pipeline
- rotateContentByQuarterTurns public

### Next
- More Phase 6 Tier 0 / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2441.1-drop-bakeItemRotate90-forward-d80d461.bundle HEAD
```
