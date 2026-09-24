# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2630.1-gallery-pack-align-bars** (base `e9c1e25`).

### This tip — Gallery still off-centre after Image (scrollbar gutters)
Follow-up to 2628/2629. Remaining causes:

1. **PackViewportGuard** forced AlwaysOn even when policy was AlwaysOff, so the
   pack was measured for a gutter-shrunken viewport then shown full-size.
2. **AlignCenter** floated that undersized pack in the larger client (phantom
   scrollbar margins / off-centre overview).
3. Viewport snapshot stayed armed after restore → deferred reassert could snap
   a good ExplicitLayout back.

Fixes:
- PackViewportGuard only forces AlwaysOn under AsNeeded; AlwaysOff measures live
- Gallery: AlignLeft|AlignTop; Image/Workspace: AlignCenter
- Consume scroll/centre snapshot when pending restore finishes
- returnToGallery: updateScrollBarPolicyForMode before restore + bar geometry settle

### Apply
```bash
git pull --ff-only …/biltoo-2630.1-gallery-pack-align-bars-e9c1e25.bundle HEAD
```
