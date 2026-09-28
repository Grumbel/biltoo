# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2729.1-spread-multi-underlay-install` (base `636e70e`).

### 2729.1 — Image-mode multi-underlay install for spread
- Root cause of vanishing second page: `clearLiveCanvas` + classicPath-only gates
- `imageModeItemForPath` / `isImageModeInstallPath`; in-place pending/replace when N>1
- Secondary member PreferCache climb from `applySpreadLayout`
- Pure `spreadstate` tests; SPREAD.md status line

### Still open
- Status bar range for active spread members
- Event-driven re-layout (drop QTimer 0/100ms sync)
- P2 cross-page text

### Apply
```bash
git pull --ff-only …/biltoo-2729.1-spread-multi-underlay-install-636e70e.bundle HEAD
```
