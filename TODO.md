# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.9-performance-panel-ui (base 2085c07).

### 2815.9
- Performance panel: metric cards, status badge (Settled/Working/Hot/Idle tick)
- FocusFull card flags >1 (needs thumtoo focus-full-no-busy-wait)
- Cleaner deltas + recent schedule log

### thumtoo (required for settle CPU)
- thumtoo-focus-full-no-busy-wait-551a360.bundle — FocusFull wait, no archive pyramid coalesce

### Prior
- 2815.8 Gallery→Image paint freeze
- 2815.7 tile LOD settle
- 2815.6 Performance panel
- 2815.5 vips concurrency 1

### Apply
```bash
git pull --ff-only …/biltoo-2815.9-performance-panel-ui-2085c07.bundle HEAD
```
