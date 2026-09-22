# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2330-session-reorder-dialog.**

Modal **Reorder Session…** dialog (Gallery + Edit menus): drag rows or Move
Up/Down/Start/End; OK applies undoable session order via SessionReorderCommand.
Filmstrip drag/keyboard/menu remain the primary strip UX (2327–2329).

Requires **thumtoo-323**. Includes 2318–2329.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2330.1-session-reorder-dialog-2f201f6.bundle HEAD
```

Next: **2331**.

## Backlog
- Gallery drag reorder
