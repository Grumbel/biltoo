# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2746.1-filmstrip-cursor-viewport` (base `636e70e`).

### Design
- docs/VIEW_AND_SELECTION.md — §8 decisions accepted; P2 progress noted

### Implemented (P2 start)
- `ChromeColors` (Select blue / View amber / Activity green / Search violet)
- Filmstrip: selection wash = blue; cursor = amber frame
- Filmstrip: **camera viewport** rectangle on cursor thumb
- `updateFilmstripChrome()` from `updateStatus()`

### Next
- P3 keyboard/modifier tooltips; P4 spread-member marks on filmstrip

### Apply
```bash
git pull --ff-only …/biltoo-2746.1-filmstrip-cursor-viewport-636e70e.bundle HEAD
```
