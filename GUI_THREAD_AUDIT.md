# Biltoo GUI-thread responsiveness audit

**Date:** 2026-09-12  
**Tree tip at audit start:** `b552c50`  
**Focus:** Gallery scrolling and archive/session load stalls; later Image ←/→ and slideshow.

## Findings

| ID | Sev | Issue | Status |
|----|-----|-------|--------|
| G1 | P0 | MTime/FileSize sort QFileInfo in comparators | **Fixed 527** |
| G2 | P0 | preparePaths exists() on GUI | **Fixed 527** |
| G3 | P0 | installDisplayPixels storms | **Fixed 528** — ≤48 installs/turn + reschedule |
| G4 | P0 | decode window 3× O(n) host passes | **Fixed 528** — pass1+1b merged |
| G5 | P1 | Toolbar zoom sync decode window | **Fixed 527** |
| G6 | P1 | setInterest URI convert on GUI | **Fixed 528** — `warmUris` worker |
| G7 | P1 | Soft completion sync decode window | **Fixed 527** |
| G8 | P1 | Metadata Exiv2 / full load on GUI | **Fixed 528** — Exiv2 worker; no GUI full load |
| G9 | P0 | Image-mode held ←/→: full LoadReplace per auto-repeat (sync `repaint`, PreferCache climb timers, title/location chrome) | **Fixed 694** — nav-hot + 80 ms settle; soft only while hot |
| G10 | P0 | Slideshow `loadImage` / PreferCache after phase-from already at target edge | **Fixed 696** — no LoadReplace while slideshow session active |
| G11 | P0 | Promote cleared dwell atlas → multi-MP `drawImage` every frame until rebuild | **Fixed 696** — transfer to-atlas on promote |
| G12 | P1 | Slideshow atlas `FastTransformation` soft upscale looked nearest-neighbour | **Fixed 697** — SmoothTransformation on pool; smooth soft fallback |
| G13 | P0 | `pathsNeedBackgroundExpand` + `canonicalImagePath` stat/exists on GUI before Indexing | **Fixed 903** — no isFile/isDir/isAvailable; absolute path only; expand off-GUI |

## Residual

- Pass 2 still walks all items for on-screen/blank detection (debounced); off-screen
  interest/`rest` lists are capped during the scan (**529**).
- setInterest still builds interest vectors on GUI (URI cache hit after warm).
- `fillImageAnalysis` still on GUI when decodedHint is present (CPU only).
- Image-mode settle still does one PreferCache climb after quiet; first soft frame
  uses async `update` while nav-hot (single-tap PreferCache ~80 ms later).
- First `ThumtooCache::isAvailable()` / `init()`: **Client::open is off-GUI**
  (tip 984). GUI `init()` schedules a pool open and returns; workers open
  synchronously under the same lock.

## Image / slideshow nav (normative)

See [SLIDESHOW.md](SLIDESHOW.md) for pure-phase ownership, phase-buffer climb, and
atlas rules. Outside slideshow, held ←/→ must not stack PreferCache on every
auto-repeat key (`setSlideshowNavHot` + settle timer in `MainWindow`).
