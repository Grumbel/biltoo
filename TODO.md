# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2815.3-no-exists-load-error` (base `2085c07`).

### 2815.3
- `formatLoadErrorMessage`: never `QFileInfo::exists` — detail from `mupdf_last_error()` only
- `emptyResultMessage` (GUI failed Open) no longer passes `allowFilesystemStat=true`
- Dropped the `allowFilesystemStat` parameter entirely

### Prior
- 2815.2 messagelog include for reportSessionOpenFailed
- 2815.1 annotated-pages list + reportSessionOpenFailed body
- 2814.2 updateStatus no GUI exists (partial; 2815.3 finishes the contract)

### Apply
```bash
git pull --ff-only …/biltoo-2815.3-no-exists-load-error-2085c07.bundle HEAD
```
