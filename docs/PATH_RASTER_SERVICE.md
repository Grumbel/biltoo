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
| **Thumtoo** | Durable soft, overview, PreferCache, full; settle keys |
| **ImageCache** | Process RAM path → best raw sample (upward-only) |
| **PathRasterService** | want / have / PreferCache plateau / optional Full; **only** host scheduler |
| **ImageView / paint** | Need edge + install; **do not** schedule PreferCache directly |

## ClimbPolicy

```text
SoftDisplay    — Gallery: Soft → PreferCache; plateau terminal for this want
EscalateToFull — Image mode + Slideshow: Soft → PreferCache → one Full
```

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
| Gallery | SoftDisplay | `ladderReady` → `applyGalleryLadderReady` |
| Image mode | EscalateToFull | `rasterImproved` / `tryInstall` |
| Slideshow | EscalateToFull | `rasterImproved` → phase buffers |

Gallery keeps `GallerySoftState` as a **prioritization mirror** (want, inflight
budget, blank tiles). `syncGallerySoftMirrorFromPathRaster` copies have/gaveUp
from this service. Decode-window scheduling calls `ensure(..., SoftDisplay)` only.

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

