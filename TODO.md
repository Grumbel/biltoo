# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2319-session-export-order-docs.**

Docs only: `docs/SESSION_EXPORT_AND_ORDER.md` (session bake export +
reorder UI backlog + File menu mode matrix). Includes 2318.

### Prior tip note

**2318 filmstrip tile stuck.**

Filmstrip could stay on LQIP while Gallery already had durable tiles:
PreferCache settled short (`g_pixelsSettled`), `climbPending` made
DisplaySurface evaluate `None`, and filmstrip never listened to
`durableTilesReady`. Selecting the row re-armed loads via navigation.

Fix: connect `durableTilesReady`; `forgetPixelsSettled` when durable-known
and host still short of strip edge; clear await so climb can re-arm.

Includes 2313–2317. Requires **thumtoo-323**.

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-323.1-try-exif-external-linkage-bd9cca0.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2319.1-session-export-order-docs-999be36.bundle HEAD
```

Next: **2320**.

## Backlog (do not lose)

- **Session image export** (Gallery): bake rotate/flip/crop → directory / .cbz / multi-page PDF; never touch originals. See `docs/SESSION_EXPORT_AND_ORDER.md`.
- **Session reorder UI**: drag or list UI to reorder session membership (filmstrip/Gallery/slideshow/export order). Not Workspace z-order. Same doc §2.
- **File menu mode gating**: hide/disable page guide + page PNG/PDF outside Workspace; export images when session non-empty.

