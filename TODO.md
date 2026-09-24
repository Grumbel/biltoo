# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2637.1-gallery-pack-always-gutter** (base `a4d3856`).

### This tip — layout for no bars → bars appear → off-centre
Pack measured the full client (AlwaysOff / no gutter reserve). AsNeeded bars
then appeared, viewport shrank, AlignCenter shifted.

- PackViewportGuard always forces AlwaysOn for measure (both gutters)
- Measure width/height only after AlwaysOn is applied
- After policy restore: two-pass sceneRect expand + refreshScrollBarGeometry
  so a post-set bar spawn resizes and re-centres the sceneRect

Keeps AlignCenter for zoom-out.

### Apply
```bash
git pull --ff-only …/biltoo-2637.1-gallery-pack-always-gutter-a4d3856.bundle HEAD
```
