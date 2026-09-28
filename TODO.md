# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2730.1-spread-status-and-sync` (base `636e70e`).

### Stack
- **2729.1** — multi-underlay install (no clearLiveCanvas wipe on soft/full)
- **2730.1** — status range + event-driven spread sync + FixedN filmstrip

### 2730.1
- Status bar: `Spread a–b/N · …` via `statusLabelText()`
- Prev/Next tips: next/previous **spread** (or page when `ByPage`)
- `scheduleSpreadSync()` coalesced QueuedConnection; statusChanged re-syncs layout
  when soft sizes arrive (replaces dual 0/100 ms timers)
- FixedN: filmstrip/`setCurrentIndex` moves membership window to anchor pair

### Still open
- P2 cross-page text selection
- Optional: stride UI (BySpread vs ByPage), CoverAlone binding toggle

### Apply
```bash
git pull --ff-only …/biltoo-2730.1-spread-status-and-sync-636e70e.bundle HEAD
```
