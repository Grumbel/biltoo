# Tile load coordinator

**Sole issuer** of grid-tile requests for an `ImageView`.

## Ownership

| Component | Role |
|-----------|------|
| `TileLoadCoordinator` | Global priority + budget; only place that calls `tickTileLod(budget>0)` |
| `ImageItem` / `TileLodController` | Viewport plan, paint, pump completions |
| `ImageView::tickPrimaryTileLod` | Forwards to coordinator |

## Priority (visible first)

1. On-screen cells with **no tiles yet** (need any coverage)
2. On-screen **incomplete** exact coverage
3. Fully covered cells (upres) **only when** no in-view cell still needs coverage

Off-screen speculative tile issue is not done here; soft/idle policy is separate.

## Non-goals

- ImageItem must not start PreferCache/tile storms on its own for global policy
- Paint path must not issue requests (plan + draw only)
