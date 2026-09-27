# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.1-f5-selection-mtime` (base `bcbb97e`).

### 2714.1 — F5 / Shift-F5: selection-only, no Gallery relayout, mtime-gated soft
- **F5 (soft):** `ThumtooCache::checkSourceChanged` compares disk mtime/size to
  Store locator; regenerates process caches + re-decode only when changed.
- **Shift-F5 (hard):** always purge process + durable Store for targets, then
  re-decode.
- **Targets:** Image mode = current image; Gallery/Workspace = selection, else
  primary/focused. Never all live items.
- **No Gallery relayout** on either path (`relayoutGallery` forced false from
  MainWindow; controller args ignored).
- **No PendingSessionBind / LoadAdd** on reload — existing items stay in the
  session (fixes random disappearances).

### Apply
```bash
git pull --ff-only …/biltoo-2714.1-f5-selection-mtime-bcbb97e.bundle HEAD
```
