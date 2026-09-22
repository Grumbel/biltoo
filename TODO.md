# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2318-filmstrip-tile-stuck.**

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
git -C biltoo pull --ff-only …/biltoo-2318.1-filmstrip-tile-stuck-999be36.bundle HEAD
```

Next: **2319**.
