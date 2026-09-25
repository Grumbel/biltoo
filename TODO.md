# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2696.1-exact-level-content-math` (base `932ed5c`).

### 2696 — exact tile math only
Strip overdraw / stretch / +1 seam hacks.

Contract:
- Level rect = exclusive `tile_cell` on `dim_at_tile_scale(content, s)`
- Content dest = level rect × `2^s` (integer)
- Src = exclusive encoded payload (cap legacy >256)
- Dest size = src × `2^s` (same numbers)

No paint-side seam workarounds. Missing ink every 256px is encode or
filter (Smooth), not dest geometry.

### Apply
```bash
git pull --ff-only …/biltoo-2696.1-exact-level-content-math-932ed5c.bundle HEAD
```
