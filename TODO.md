# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2736.1-spread-p4-binding-rtl` (base `636e70e`).

### Spread stack — P0–P4 core

| Phase | Status |
|-------|--------|
| P0–P1 reading surface | complete |
| P2 text | complete |
| P3 TTS + Gallery ring | complete |
| P4 binding / direction / N | 2736.1 |

### 2736.1
- View menu: Strict pairs vs Cover alone binding
- Spread direction LTR / RTL (`layoutSpread` mirrors slots)
- Fixed-N preference 2 / 3 / 4 (hard cap 8 still enforced)
- Double-view uses current binding + N prefs

### Still open
- Vertical spread direction
- Persist prefs to project/settings

### Apply
```bash
git pull --ff-only …/biltoo-2736.1-spread-p4-binding-rtl-636e70e.bundle HEAD
```
