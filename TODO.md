# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2661.1-session-search-index` (base `9740316`).
**Thumtoo:** still needs `339.1-text-block-id` for block_id Find.

### Done
- `SessionSearchIndex` — sparse Find tags by SessionImageId (+ path fallback).
- Filmstrip: yellow top-left badge when row is tagged.
- Gallery: soft yellow wash + ring on tagged tiles.
- Doc scan commits path→id hits; page-local Find tags current row.
- Generation guard against stale workers.
- Unit tests: sessionsearchindex (6 passed).

### Not ItemWorld
Search hits are query-scoped UI state, not durable appearance components.

### Apply
```bash
git pull --ff-only …/biltoo-2661.1-session-search-index-9740316.bundle HEAD
ctest -R sessionsearchindex --output-on-failure
```
