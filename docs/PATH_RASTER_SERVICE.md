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

## Next (not this tip)

- Move Image-mode PreferCache climb onto PathRasterService
- Move gallery soft schedule onto the same service (or a thin wrapper)
