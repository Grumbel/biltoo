# PathRasterService

**One climb policy for path → host raster.**

## Problem

PreferCache / soft scheduling lived in:

- `ImageView::ensureImageModeQualityClimb`
- `ImageView::preloadSlideshowImage` / `loadSlideshowSample`
- Gallery soft state (`m_gallerySoft`)
- Ad-hoc `scheduleDisplayPixels` retries after shortfall

Slideshow stayed soft when those paths disagreed (atlas coverage, missing
re-queue, duplicate schedules).

## Authority

| Layer | Role |
|-------|------|
| **Thumtoo** | Durable compressed ladder + PreferCache decode |
| **ImageCache** | Process RAM path → best raw sample (upward-only) |
| **PathRasterService** | Per-path want/have, schedule soft+display, re-queue until adequate or gave-up |
| **ImageView / paint** | Consume `ImageCache` / phase buffers; **do not** schedule PreferCache |

## API

```text
ensure(path, wantEdge, knownNative?)  → raise target, pump thumtoo
noteDelivery(path, requestEdge, image) → put cache, emit rasterImproved, pump
best(path) / haveEdge(path)
invalidateAll()                        → session switch
```

## Slideshow

`preloadSlideshowImage` only calls `m_pathRaster->ensure` and installs current
cache. Phase buffers update on `rasterImproved`. Atlas rebuild policy stays on
ImageView (viewport-sized texture).

## Consumers

| Consumer | How |
|----------|-----|
| Slideshow | `preloadSlideshowImage` → `ensure`; install on `rasterImproved` |
| Image mode | `ensureImageModeQualityClimb` → `ensure`; install on `rasterImproved` / ladderReady |
| Gallery | `scheduleGalleryDecode` → `ensure`; install on `ladderReady` → `applyGalleryLadderReady` |

Gallery keeps `GallerySoftState` for visibility prioritization, concurrency budget
(`inflight`), and gave-up tracking. Climb requests no longer use a parallel
`QThreadPool` + `ImageLoader::loadThumbnail` path.

## Residual cleanup

- `ImageModeClimbState` / `m_imageModeClimb` removed (PathRasterService owns climb).
- Dead slideshow `m_ss*MotionBaseMs` removed (unitless phase owns progress).
- Gallery pool soft climb helpers removed (PathRasterService + ladderReady only).

## Edit full raster

Crop / Workspace content bake uses `ImageView::fullRasterForEdit`: **ImageCache**
when the sample already covers native logical size (after Image-mode / thumtoo
full climb), otherwise `ImageLoader::load` + `ImageCache::put`. Cold crop enter
can still decode on the caller thread; warm re-enter after Image mode does not.

## Crop full raster

Crop enter uses provisional host pixels when native is not yet cached, then
`requestCropFullRaster` → thumtoo `scheduleFullPixels` (pool `ImageLoader::load`
fallback). `maybeUpgradeCropFullRaster` rescales the draft rect on delivery.
Apply is blocked while still awaiting native coverage.

## Next

- Optionally fold GallerySoftState have/gaveUp into PathRasterService if duplication hurts
