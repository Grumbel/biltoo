# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2321-session-export-verify.**

Verify/fix for 2320 session export:
- Bake always full-decodes then clamps (crop stays native-space)
- `updateFileExportActions` on session open/clear
- Includes tidy in mainwindow_print.cpp

Requires **thumtoo-323**. Includes 2318–2320.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2321.1-session-export-verify-999be36.bundle HEAD
```

Next: **2322**.

## Backlog
- Session reorder UI
- Export progress/cancel
