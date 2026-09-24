# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2471.2-fix-moc-notify-gallery** (base `7d823d8`).

### Fix MOC after GallerySizeResolve ownership
- `notifyGallerySizeResolveFinished()` had an inline body under `signals:` (invalid for moc)
- Moved to public host API next to other `notify*` wrappers; signal stays pure

### Prior in this tip
- Workspace page-guide / group / item-drag move+release on WorkspaceController

### Apply
```bash
git pull --ff-only …/biltoo-2471.2-fix-moc-notify-gallery-7d823d8.bundle HEAD
```
