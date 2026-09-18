<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# PathRasterService

**One climb policy for path → host raster.**

**Normative contract:** [THUMTOO_HOST_CONTRACT.md](THUMTOO_HOST_CONTRACT.md)  
Bands and codecs: [PERFORMANCE.md](PERFORMANCE.md)

## Problem

PreferCache / soft scheduling lived in Image-mode climb, slideshow preload,
gallery soft state, and ad-hoc `scheduleDisplayPixels` retries. PreferCache
**BestAvailable** (e.g. overview 1024 for want 2048) was misread as a permanent
failure, and consumers invented different recoveries.

## Authority

| Layer | Role |
|-------|------|
| **Thumtoo** | Session soft, overview, PreferCache (TileSynth), durable tiles, full; settle keys |
| **ImageCache** | Process RAM path → best raw sample (upward-only) |
| **PathRasterService** | want / have / PreferCache plateau / optional Full; **only** host scheduler |
| **ImageView / paint** | Need edge + install; **do not** schedule PreferCache directly |

## ClimbPolicy

```text
SoftDisplay / EscalateToFull — Soft → PreferCache (≤1024 effective) → Full when want > 1024
```

**Progressive order (EscalateToFull):** Soft and Prefer may share a plan while
soft is uncovered. **Full is not scheduled until PreferCache has plateaued**
(`preferGaveUp`) short of need. Same-plan Soft+Prefer+Full delayed intermediate
paints (long blank/soft, then jump to native).


thumtoo PreferCache above soft max is overview-clamped to 1024. Whole-frame
edges above that need **Full**. Gallery only ensures a concurrency-bounded
visible set so Full is not N× archive extract.

PreferCache does **not** guarantee `got ≈ request`. Plateau is normal. Raising
`want` past `lastDisplayWant` clears the plateau latch (gallery/Image zoom).

## API

```text
ensure(path, wantEdge, knownNative?, policy = SoftDisplay)
noteDelivery(path, requestEdge, image) → put cache, emit rasterImproved, pump
best / haveEdge / wantEdge / isGaveUp / isClimbPending
clearPreferGaveUp(path)   — rare; prefer raising want via ensure
invalidateAll()           — session switch
```

## Consumers

| Consumer | ensure policy | Install |
|----------|---------------|---------|
| Gallery | *(none — LQIP + tiles)* | `applyGalleryLadderReady` accepts LQIP only |
| Image mode | EscalateToFull (cold) / tiles when durable | `rasterImproved` / `tryInstall` |
| Slideshow | SoftDisplay (screen-fit) → tiles/TileSynth | phase buffers + optional tile paint |
| Filmstrip | *(not PathRaster — `scheduleFilmstripTilePixels`)* | LQIP + TileSynth |

Gallery does not call `ensure`. Decode window installs LQIP and drives
TileLoadCoordinator. Historical name `GallerySoftState` tracks decode-window
budget only (not PreferCache soft climb).

**Soft-band and Display PreferCache plans:** use
`ThumtooCache::scheduleTileSynthOrPyramid` only (never bare PreferCache soft
encode). Full (`scheduleFullPixels`) remains for EscalateToFull after Prefer
plateau.

## Edit / crop full raster

Crop and Workspace content bake still use `fullRasterForEdit` /
`scheduleFullPixels` for **edit-quality** native coverage. That is separate from
display climb (Display band ≤2048). See contract §2 Full vs Display.

## Anti-patterns

- PreferCache retry via `forgetPixelsSettled` + `clearPreferGaveUp` in ImageView
- Per-tick slideshow `ensure` for look-ahead (once per `toIdx` only)
- Assuming PreferCache returns want edge
- Second climb state machines / `scheduleDisplayPixels` outside PathRasterService
- Pool workers calling thumtoo schedule APIs directly (use `requestEscalateClimb`)
- ImageView cold LoadReplace using bare `schedulePixels` (use `requestEscalateClimb`)

Filmstrip and Image underlay no longer use soft PreferCache encode (see
[FILMSTRIP_LAYOUT.md](FILMSTRIP_LAYOUT.md), [GALLERY_SOFT.md](GALLERY_SOFT.md)).

