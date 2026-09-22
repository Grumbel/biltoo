# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2329-session-reorder-public-api.**

Fix: `applySessionOrder` / `selectSessionIdsOnFilmstrip` must be public for
`SessionReorderCommand` (anonymous-namespace QUndoCommand). Implement the
missing `selectSessionIdsOnFilmstrip` body (map SessionImageId → filmstrip
indices via `setSelectedIndices`).

Requires **thumtoo-323**. Includes 2318–2328.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2329.1-session-reorder-public-api-2f201f6.bundle HEAD
```

Next: **2330**.

## Backlog
- Reorder session dialog
- Gallery drag reorder
