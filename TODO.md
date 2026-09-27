# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.3-forget-size-memo-on-reload` (base `bcbb97e`).

### 2714.3 — Soft/hard reload forgets process size memo
- `ThumtooCache::forgetCachedSize` clears ProcessMemos size for a path.
- All F5 regenerate paths call it with size-book take so `tileNativeSize`
  cannot keep a pre-change WxH while tiles rebuild (stretch).

### 2714.2 — Clear tile RAM when content size changes (included)
### 2714.1 — F5 selection-only / mtime / no relayout (included)

### Apply
```bash
git pull --ff-only …/biltoo-2714.3-forget-size-memo-on-reload-bcbb97e.bundle HEAD
```
