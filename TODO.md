# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2680.1-seam-paint-order` (base `932ed5c`).

### 2680 — paint order + directional overdraw
Root cause of remaining grid seams / “disappearing lines”:
- ExactTile expands **right/bottom**, but paint sorted **L→R / T→B**, so the
  *next* exclusive tile was painted last and **erased** the shared strip.
- Symmetric `±gap` overdraw let the next cell eat ~0.75–1 content px into the
  previous exclusive rect (detail lines near x=256k vanished).

**Fix:** sort **R→L / B→T**; overdraw only `+right/+bottom` (both ImageItem and
`paint_draw_plan`).

### Prior in stack
- 2679 last-tile coverage + overdraw clamp
- 2678 research doc
- 2677 smooth only at 1×/2×

### Residual
- JPEG dual-encode of the 257 shared strip (colour step at seam, not a gap).
- Re-prepare tiles if Store still has pre-overlap 256× cells mixed with 257.

### Apply
```bash
git pull --ff-only …/biltoo-2680.1-seam-paint-order-932ed5c.bundle HEAD
```
