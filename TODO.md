# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2684.1-smooth-1to1-overlap` (base `932ed5c`).

### 2684 — smooth seams vs pixel-exact
- **Smooth off:** exclusive src→dest (checkerboard continuous; no filter seams).
- **Smooth on:** ExactTile with 257 bitmap expands dest **1:1** with full source
  (not 257→256 scale). Bilinear can sample the shared edge. Paint R→L/B→T so
  the strip wins.

### Apply
```bash
git pull --ff-only …/biltoo-2684.1-smooth-1to1-overlap-932ed5c.bundle HEAD
```
