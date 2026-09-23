# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2387-gui-budget-store-off-gui** (on top of `660c49c` stack).

Includes **2381–2386**.

### 2387
- Store `loadContentAppearance` / `cachedFileStat` never on GUI.
- Appearance seed is async (worker + queued apply).
- GUI_BUDGET default **25ms**; scopes on paint, mode switch, sizeReady,
  layout/decode, filmstrip, install paths.

**Next:** RC smoke under load; watch stderr for `GUI_BUDGET EXCEEDED`.
Optional: `BILTOO_GUI_BUDGET_STRICT=1`.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2387.1-gui-budget-store-off-gui-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2386
- [x] 2387 GUI budget + Store off GUI
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
