# Tile load coordinator

Keeps visible items' tile **plans and demand leases** current. It does **not**
issue requests — `tilelod::TileScheduler` is the sole issuer (global cap,
priority across paths). Normative: [TILE_STATE_MACHINE.md](TILE_STATE_MACHINE.md).

## Ownership

| Component | Role |
|-----------|------|
| `TileLoadCoordinator` | Collect visible tile-band items; `tickItemTileLod` each (plan + lease renew); cancel PreferCache once per path entering the band |
| `ImageItem` / `TileLodController` | Viewport plan, demand, paint, change hook → repaint |
| `DisplayPipelineController::tickPrimaryTileLod` | Runs the coordinator; re-arms at 250 ms only while a visible item is Loading |
| `TileScheduler` | Issue / cancel / retry / stall watchdog (event-driven) |

## Order

Visible items are sorted (in view, zero-tile first, larger on screen first)
only so that the most needy renew first if the wall budget runs out on huge
galleries. **Every** visible item is refreshed — the old one-target limit
starved Workspace items.

## Non-goals

- Paint path must not issue requests (plan + draw only; demand publishing is
  idempotent and never issues by itself).
- No per-item issue budgets: priority classes on demand decide order.
