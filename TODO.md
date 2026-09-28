# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2749.1-fix-cursor-members` (base `9395b3e`).

### Done
- Fix: declare `m_cursorIndex` / `m_cursorViewportNorm` on ThumbnailBar
  (omitted by filmstrip cursor chrome commit; broke the build)

### View/selection language track
- P0–P5 design + filmstrip + chrome unification largely complete
- Remaining polish: theme-aware mapping, more status-string terminology

### Apply
```bash
git pull --ff-only …/biltoo-2749.1-fix-cursor-members-9395b3e.bundle HEAD
```
