# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2322-session-export-harden.**

Session export hardening after verify:
- JPEG: convert exotic formats before save
- CBZ/PDF: create parent directories
- PDF: setPageSize before each page; zero margins
- updateFileExportActions after project open

Includes 2318–2321. Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2322.1-session-export-harden-999be36.bundle HEAD
```

Next: **2323** (reorder UI or export progress).

## Backlog
- Session reorder UI (`docs/SESSION_EXPORT_AND_ORDER.md` §2)
- Export progress / cancel
