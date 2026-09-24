# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2613.1-fix-peel-fallout-includes-raise** (base `7d823d8`).

### This tip
Build fallout from peels:
- HUD routers: `targetHasContentAppearance` → `hostImage().…`
- View routers: restore includes (`GUI_BUDGET`, `biltooModeDbg`, `ViewModeFlags`)
- Chrome raise/lower: `hostWorkspace().raiseItem/lowerItem`

### Apply
```bash
git pull --ff-only …/biltoo-2613.1-fix-peel-fallout-includes-raise-7d823d8.bundle HEAD
```
