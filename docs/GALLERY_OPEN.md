<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery open pipeline

Authoritative order for Gallery session open. Later phases must not invert
earlier fences for a path or for the session.

## Policy (summary)

| Stage | Allowed | Forbidden |
|-------|---------|-----------|
| **Size gate** (session) | `request_size`, size book / process memo, plan of definitive sizes | Tiles, PreferCache soft climb, stand-in plan sizes; **requiring ImageCache underlay to settle** |
| **After gate** | Materialize visible ∩ plan, underlay install, tiles | — |

**Size gate is size-only.** A path settles when the host size book or process
memo has a definitive size (or failure). Missing `ImageCache` underlay must
**not** re-arm the gate on Gallery↔Image mode switches.

**Underlay** = EMB (container thumb) or LQIP (ThumbHash) from the **same Store
row as size** (`request_size` SizeReply) when a probe runs. Never generate
underlay; never standalone `get_lqip` as a product path. GUI reads underlay only
from `ImageCache`. Underlay is opportunistic: dark chrome until seeded;
PreferCache / later probes may fill it without blocking the size gate.

**Tiles** wait until the **full session** size set has settled. Underlay may
appear **per path** once that path has a definitive size and a cached sample
(policy B).

## Phases

```
PHASE 0 — Session fence
  cancelSizeProbes()          // generation++
  ImageCache::clear()         // on session replace only (not mode switch)
  clear size book / live items / virtual plan

PHASE 1 — Size gate (active until every path settled)
  for path in session:
    if definitive size (book or process memo):
      adopt into book if needed; count warm
    else:
      enqueue request_size(path)

  on SizeReply (generation live):
    sizeBook.noteDefinitive (or fail)
    if underlay blob: ImageCache.put   // opportunistic, not a settle condition
    noteProbeSettled
    scheduleSizeGatePlanRefresh()  // coalesced plan + virtual window

  while active:
    YES: underlay via tryInstallGalleryUnderlay / virtual paint from ImageCache
    NO:  TileLoadCoordinator, scheduleTilePyramid for viewport climb

PHASE 2 — Layout + materialize
  rebuildVirtualPlan()     // definitive | failed only; no 1000² stand-ins
  syncVirtualWindow()      // visible ∩ plan → live items
  tryInstallGalleryUnderlay on each live cell
  paintVirtualPlaceholders: ImageCache underlay if hot, else dark chrome

PHASE 3 — Tiles (gate complete only)
  updateDecodeWindow: underlay pass, then tile issue for visible blanks
```

## Single underlay install

All Gallery underlay attaches go through:

`DisplayPipelineController::tryInstallGalleryUnderlay(ImageItem *)`

(sizeReady, syncVirtualWindow, decode-window blanks, scheduleGalleryDecode).

## Dependencies (must not invert)

```
request_size  →  sizeBook definitive  →  plan geometry  →  gate complete → tiles
request_size  →  ImageCache underlay  →  SoftPreview / virtual paint   (parallel, optional)
```

## Related

- [GALLERY_PIXELS.md](GALLERY_PIXELS.md) — LQIP / EMB / tiles product layers
- [THUMTOO_HOST_CONTRACT.md](THUMTOO_HOST_CONTRACT.md) — Store vs host cache
