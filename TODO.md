# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2439.1-tier0-privatize-iv-only** (base `d80d461`).

### Phase 6 Tier 0 (continued)
- setItemIntrinsicSize private (2438)
- **Privatized imageview-only methods (2439):**
  - `sessionIdMatchesPath`
  - `restoreStickyPanAnchor`
- Kept public for pipeline: `captureStickyPanAnchor`, `clearTextSelection`, bake helpers

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- sessionIdMatchesPath / restoreStickyPanAnchor not in public header/host
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers public for pipeline

### Next
- More Phase 6 Tier 0 / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2439.1-tier0-privatize-iv-only-d80d461.bundle HEAD
```
