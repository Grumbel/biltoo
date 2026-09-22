# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2324-export-cleanup.**

Export path re-verified (20/20 structural checks). Cleanup:
- Remove dead sameOrUnderPath stub
- updateFileExportActions after createMenus (initial empty session)

Requires **thumtoo-323**. Includes 2318–2323.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2324.1-export-cleanup-999be36.bundle HEAD
```

Next: **2325** — session reorder UI or export progress/cancel.

## Backlog
- Session reorder UI (`docs/SESSION_EXPORT_AND_ORDER.md` §2)
- Export progress / cancel
