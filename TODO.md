# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2687.1-chrome-keep-selection` (base `932ed5c`).

### 2687 — workspace chrome flip/rotate keeps selection
`beginHandleInteraction` activates flip/90° chrome and returns true without a
continuous handle. `tryMousePressWorkspaceChrome` required
`hasContinuousHandle()`, so the press fell through and QGraphicsView cleared
selection. Toolbar rotate/flip never go through that path. Fix: accept the
event on any successful handle interaction.

### Apply
```bash
git pull --ff-only …/biltoo-2687.1-chrome-keep-selection-932ed5c.bundle HEAD
```
