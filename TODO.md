# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2446.1-restore-sticky-pan-public** (base `d80d461`).

### Fix
- **`restoreStickyPanAnchor` public host again** (paired with `captureStickyPanAnchor`)
- Characterization tests call both; privatize in 2439 broke the test build

### Phase 5 ownership — still complete
- Sole friend: DisplayPipelineController
- No ImageView pixel forward TUs

### Next
- Dual ImageView (0.3) / optional Tier 0 polish

### Apply
```bash
git pull --ff-only …/biltoo-2446.1-restore-sticky-pan-public-d80d461.bundle HEAD
```
