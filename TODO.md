# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2395-gui-budget-chain-slice** (on top of `660c49c` stack).

Includes **2381–2394**.

### 2395
- GUI_BUDGET reports **chained-turn** totals across tight sequential scopes
- sizeReady chunk outer budget
- virtual window materialize 12ms-sliced + re-arm

**Next:** RC smoke under load; watch `chained-turn` in stderr.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2395.1-gui-budget-chain-slice-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2394
- [x] 2395 GUI budget chain + virtual window slice
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
