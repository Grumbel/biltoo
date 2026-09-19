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

`hasAnyTile` is true when the item has a live session with Succeeded tiles **or**
`tileLodHasPathRam()` (process-wide `TileLodRegistry` still holds Succeeded tiles
for the path). A→B→A and Gallery restore after Image mode are not treated as
cold zero-tile cells.

Off-screen speculative tile issue is not done here; soft/idle policy is separate.
Neighbor overview fill is `TileNeighborPrefetch` (Image-mode nav settle).

## Non-goals

- ImageItem must not start PreferCache/tile storms on its own for global policy
- Paint path must not issue requests (plan + draw only)

## Issue order (TileSession)

Always **coarsest → finest**: parent scales before exact target. Scale 0
(full-res) only after at least one coarser tile has succeeded. Display is
instant overview, then progressive refinement.
