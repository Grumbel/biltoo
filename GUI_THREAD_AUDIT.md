# Biltoo GUI-thread responsiveness audit

**Date:** 2026-09-12  
**Tree tip at audit start:** `b552c50`  
**Focus:** Gallery scrolling and archive/session load stalls.  
**Method:** Full pass of all `src/*.cpp` units.

## Progress

All units reviewed. See findings below. Follow-ups **G1, G2, G5, G7** applied in tip **527**.

## Findings (summary)

| ID | Sev | Issue | Status |
|----|-----|-------|--------|
| G1 | P0 | MTime/FileSize sort: QFileInfo in comparators on GUI | **Fixed 527** — background worker + sync snapshot fallback |
| G2 | P0 | preparePaths: exists() per path on GUI at open | **Fixed 527** — no exists filter; missing files miss in index |
| G3 | P0 | installDisplayPixels / materializeDisplay on GUI | Open (paint budget already limits scale) |
| G4 | P0 | updateGalleryDecodeWindow 3× O(n) | Open (debounced on scroll) |
| G5 | P1 | Toolbar zoom sync decode window | **Fixed 527** — schedule 120 ms |
| G6 | P1 | setInterest URI work on GUI | Open (capped speculative) |
| G7 | P1 | Soft completion sync decode window | **Fixed 527** — schedule 48 ms |
| G8 | P1 | Metadata Exiv2 on GUI | Open |

## Mitigations already present before 527

Scroll/Ctrl+wheel debounce, BoundingRect Gallery, O(n) pass2, interest speculative≤12, background archive expand, soft priority, display downsample (526), status debounce.

