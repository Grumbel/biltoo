# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2764.1-filmstrip-size-orient` (base `7a2bd7b`).

### 2764.1
- Place filmstrip after other docks exist so `InitialOption` preferred extent is honoured (not 50% height)
- `placeFilmstripDock` + `applyFilmstripExtentConstraints` (min/max/sizeHint)
- Edge detect vs central widget in global coords; apply orientation + constraints together
- `showEvent` + longer orientation sync (0/50/200 ms)

### Apply
```bash
git pull --ff-only …/biltoo-2764.1-filmstrip-size-orient-7a2bd7b.bundle HEAD
```
