# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2846.1-filmstrip-open-focus (on `ea477d6` + agent stack).

### 2846.1
- Filmstrip open-focus norms use **painted content rect** (letterbox), not full cell.
- Same-index click/double-click still applies sticky pan (`refreshSameCurrentIndex`).
- Image-mode click emits open-focus before navigation; same-row re-clicks emit
  `indexNavigated` so the host restores pan.
- **Filmstrip Reorder** is opt-in (Edit menu, off by default). Off: left-drag pans
  the strip; internal session-row drop ignored. Gallery/Workspace placement drag
  unchanged. Reorder Session… dialog still available.

### Prior
- 2845.1 soft F5 process caches (needs thumtoo-011.1 for durable EPUB layout keys).

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
