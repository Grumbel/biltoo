# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.16-lqip-consistency` (base `bcbb97e`).

### LQIP recovery + consistency debug
- `ThumtooCache::checkUnderlayConsistency(path, includeStore, cb)` — process vs
  Store LQIP/EMB/tiles; issue lines for false-positive memos and missing rows.
- `scheduleEnsureLqipFromTiles` — Store LQIP from free tile data when missing
  (kill mid-pyramid). Never opens source.
- Debug menu: **Check underlay consistency (selection / session)…**
- durableTilesReady always schedules ensure; seed still runs when process empty.

### Apply
```bash
git pull --ff-only …/biltoo-2714.16-lqip-consistency-bcbb97e.bundle HEAD
```
