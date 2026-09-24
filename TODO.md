# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2440.1-privatize-bakeItemFlip** (base `d80d461`).

### Phase 6 Tier 0 (continued)
- **`bakeItemFlip` private (2440)**
  - ImageItem chrome → `hostDisplayPipeline().bakeItemFlip`
  - Toolbar actions → `m_displayPipeline.bakeItemFlip`
  - `bakeItemRotate90` already private; `rotateContentByQuarterTurns` stays public

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- bakeItemFlip not in public imageview.h
- Full nix build not run in this sandbox

### Notes
- `imageview.h` ~695 lines; ~218 public methods (Tier 0 exit: &lt;1000 lines, &lt;260 public)

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers public for pipeline
- rotateContentByQuarterTurns public (framing after bake)

### Next
- More Phase 6 Tier 0 / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2440.1-privatize-bakeItemFlip-d80d461.bundle HEAD
```
