# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2332-multiselect-reorder-gallery-drop.**

- Filmstrip multi-select drag: snapshot selected rows at press so the
  session-row mime carries the full selection (not only the pressed thumb).
- Gallery drop from filmstrip: internal selection reorders the session by
  drop target (no path append/duplicate). External drops still append.

Requires **thumtoo-323**. Includes 2318–2331.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2332.1-multiselect-reorder-gallery-drop-2f201f6.bundle HEAD
```

Next: **2333**.

## Backlog
- Gallery-canvas drag reorder (drag tiles on the packed canvas itself)
