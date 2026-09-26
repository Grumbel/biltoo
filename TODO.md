# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.14-no-failed-retry-error` (base `b9c3473`).

### 2713.14 — No Failed tile retry spam; ERROR when settled
- **Removed** 750ms Failed re-issue (LOADING↔WAITING loop on incomplete pyramid).
- Failed is **terminal for the viewport generation** (reopens only on plan/gen bump).
- Overlay: `ERROR failed/visible` when settled with failures; not WAITING.
- Coverage: `failed`/`missing` counts + `settled()`.

Root cause of persistent exact-miss cells is still Store/encode (incomplete
pyramid or request returning null). Host must not poll; thumtoo must encode
on request_tile miss or return a clean permanent miss.

### Apply
```bash
git pull --ff-only …/biltoo-2713.14-no-failed-retry-error-b9c3473.bundle HEAD
```

## Next
Trace why exact cells return null while others succeed (page with plan U holes).

## Prior
2713.13 retry (reverted in spirit); underlay slot; Kill Soft; thumtoo-344.2
