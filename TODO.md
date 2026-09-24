# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2447.1-ownership-closed-refactor-pointer** (base `d80d461`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake ownership: CLOSED**
- **Phase 6 Tier 0 exit: MET** (`imageview.h` ~693 lines, ~217 public)
- Fix 2446: `restoreStickyPanAnchor` public (characterization)
- No privatized methods referenced from `tests/` (scan clean)

### Verification (static)
- Friend: only DisplayPipelineController
- Pixel mutators only in displaypipelinecontroller.cpp
- No imageview_bake.cpp / imageview_rematerialize.cpp
- Characterization view.* calls: no private overlap

### Next (product)
- Dual ImageView (0.3) — shared pipeline + ItemWorld
- Optional: further Host extraction (paint/input) per REFACTOR.md

### Apply
```bash
git pull --ff-only …/biltoo-2447.1-ownership-closed-refactor-pointer-d80d461.bundle HEAD
```
