# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2699.1-new-clears-loading-tiles` (base `932ed5c`).

### 2699.1 — New session clears "Loading tiles…"
File → New called `clearWorkspace` → sizeResolve.cancel(), but cancel only
clears the centre HUD when the **size gate** was active. "Loading tiles…" is
set by the Gallery decode soft HUD and stayed stuck after an empty session.

clearWorkspace now always clearCentreProgress + stopDecodeWatchdog; newSession
clears again after mode switch.

### 2698.1 — Gallery size probe vs pack scale race
### 2697.1 — drop kTileOverlap symbol
### 2696 — exact tile math only

### Apply
```bash
git pull --ff-only …/biltoo-2699.1-new-clears-loading-tiles-932ed5c.bundle HEAD
```
