# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2697.1-drop-ktileoverlap` (base `932ed5c`).

### 2697.1 — drop kTileOverlap symbol
Dead `kTileOverlap = 0` removed from `tile_types.hpp` (matches thumtoo kill).
Docs table updated. Paint path was already exclusive exact math (2696).

### 2696 — exact tile math only
Contract:
- Level rect = exclusive `tile_cell` on `dim_at_tile_scale(content, s)`
- Content dest = level rect × `2^s` (integer)
- Src = exclusive encoded payload (cap legacy >256)
- Dest size = src × `2^s` (same numbers)

No paint-side seam workarounds.

### Apply
```bash
git pull --ff-only …/biltoo-2697.1-drop-ktileoverlap-932ed5c.bundle HEAD
```
