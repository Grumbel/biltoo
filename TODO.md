# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2396-virtual-offline-darker** (on top of `660c49c` stack).

Includes **2381–2395**.

### 2396
Offline virtual plan cells are slightly darker than live blank chrome so
materialize / LQIP is visible as a subtle lift.

**Next:** Diagnose empty ImageCache after sizeReady if LQIP still never shows;
RC smoke.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2396.1-virtual-offline-darker-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2395
- [x] 2396 virtual offline darker chrome
- [ ] RC smoke / LQIP ImageCache path if still blank
- [ ] VERSION 0.2.0 + tag
