# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.13-failed-tile-retry` (base `b9c3473`).

### 2713.13 — Failed exact tiles retry (WAITING recovery)
Incomplete Store pyramid: miss → Failed same generation → never re-issue →
bottom row stuck on parent stand-in, overlay **WAITING**.
Re-open Failed visible keys after 750ms backoff when nothing is in flight.

### Apply
```bash
git pull --ff-only …/biltoo-2713.13-failed-tile-retry-b9c3473.bundle HEAD
# still pair thumtoo-344.2 for SizeReply EMB
```

## Prior
2713.12 underlay ensure_lqip; 2713.10–11 underlay slot/seed; Kill Soft A–D

## Next
Runtime: WAITING should flip to LOADING then COMPLETE as pyramid fills

## Roadmap
docs/KILL_SOFT.md, docs/TILE_DRAW_INVESTIGATION.md
