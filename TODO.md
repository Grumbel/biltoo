# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2810.5-annot-point-map-tests` (base `2085c07`).

### 2810.5
- Unit tests: mapDisplayPointToSource ↔ mapSourcePointToDisplay round-trip
  for all turns × flips (+ crop sample). Run: contentxform_test

### Verified (code review, no Qt in sandbox to execute)
- Annot before gallery click; draft sid finish/chrome/clear/delete
- Point maps used by viewToPage / pageToScene

### Next
- Run contentxform_test locally
- Manual: rotate 90° + pen; Gallery non-primary tile
- Tool palette unification (docs/TOOL_UNIFICATION.md)

### Apply
```bash
git pull --ff-only …/biltoo-2810.5-annot-point-map-tests-2085c07.bundle HEAD
```
