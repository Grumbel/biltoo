# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2814.2-load-error-no-gui-stat` (base `2085c07`).

### 2814.2
- formatLoadErrorMessage(path, allowFilesystemStat): GUI must pass false
- Report load error once (no per-statusChanged MessageLog spam)
- Root cause of “slow reopen / filmstrip stuck”: QFileInfo::exists on NFS in updateStatus

### Prior
- 2814.1 load error UI (had the NFS bug)

### Apply
```bash
git pull --ff-only …/biltoo-2814.2-load-error-no-gui-stat-2085c07.bundle HEAD
```
