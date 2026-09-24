# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2445.1-dual-imageview-prereqs** (base `d80d461`).

### Phase 5 ownership — COMPLETE (verified again)
- Sole friend: DisplayPipelineController
- Pixel mutators (`setSourceImageReady` / `setPreviewImage` / `clearDecodedPixels` /
  `setIntrinsicSize`): only in displaypipelinecontroller.cpp
- No ImageView bake/rematerialize forward TUs
- Controllers use hostDisplayPipeline for install

### Phase 6 Tier 0 — exit met
- imageview.h ~693 lines; ~217 public methods

### Dual ImageView (0.3)
- Prerequisites documented in IMAGEVIEW_ITEM_OWNERSHIP.md
- Shared pipeline + ItemWorld; no second pixel authority
- Open: two-surface shell, focus model, per-surface framing

### Residual (single view, not blocking)
- Interactive grade live-grade + filmstrip emit
- Bake host helpers public for pipeline
- rotateContentByQuarterTurns public

### Next
- Dual ImageView shell design / spike (0.3 product)
- Optional Tier 0 privatize polish

### Apply
```bash
git pull --ff-only …/biltoo-2445.1-dual-imageview-prereqs-d80d461.bundle HEAD
```
