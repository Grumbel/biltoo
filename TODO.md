# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2702.1-tile-content-edge-stretch` (base `932ed5c`).

### 2702.1 — Coarse tile dest stretch + Image framing scale
Exclusive-only `tile_content_rect` (`level * 2^s` with no native-edge stretch)
left a floor-half residual ring vs soft underlay — coarse/lowres tiles looked
wrongly scaled on Image ←/→. Restore last-column/row stretch to content edge
for **paint dest** only (encode/source stay exclusive).

Also: sticky Fit/Fill/Actual force identity item scale before framing so
leftover Gallery pack scale cannot poison tile density (dpc) during nav.

### 2701.1 — Gallery wrong scale / soft F5 no-op
### 2700.1 — Sticky zoom survives Gallery

### Apply
```bash
git pull --ff-only …/biltoo-2702.1-tile-content-edge-stretch-932ed5c.bundle HEAD
```
