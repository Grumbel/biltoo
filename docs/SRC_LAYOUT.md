# Source tree layout (0.2.0)

Flat `src/` → domain subdirectories. Pattern matches `src/tilelod/`:
`#include "session/sessiondocument.h"` with `target_include_directories(… src)`.

## Phases

| Phase | Directory | Move | Status |
|-------|-----------|------|--------|
| 1 | `src/session/` | Session document, appearance, expand/export/open/sort/reorder, pack order views | **done (2349)** |
| 2 | `src/crop/` | `crop*`, crop appearance command | **done (2350)** |
| 3 | `src/gallery/` | gallery controller, layout, pack, size-resolve, decode SM/book | **done (2351)** |
| 4 | `src/shell/` | `mainwindow*`, icons, help, centre progress, default apps, prefs/panels | **done (2352)** |
| 5 | `src/display/` | imagecache, pipeline, quality/edge, surface, path raster, climb SM | **done (2353)** |
| 6 | `src/workspace/` | workspace controller, geometry/nav, background dialog, group transform, page guide, stack geometry | **done (2354)** |
| 7 | `src/attention/` | attention controller, session, geometry | **done (2355)** |
| 8 | `src/slideshow/` | controller, clocks, phase/atlas/motion policies, settings dialog, motion-scroll chrome | **done (2356)** |
| 9 | `src/hud/` | hud model, appearance, flash, geometry | **done (2357)** |
| 10 | `src/text/` | text layer geometry/session, search policy | **done (2358)** |
| 11 | `src/shell/` (+fold) | toc panel, thumbnail bar, keyboard shortcuts, EPUB layout dialog | **done (2359)** |
| 12 | `src/color/` | color adjust pipeline + commit bag | **done (2360)** |
| 13 | `src/host/` | ThumtooCache, process memos, size probe (host↔Store glue) | **done (2361)** |
| 14 | `src/display/` (+fold) | TileLoadCoordinator, TileNeighborPrefetch | **done (2362)** |
| 15 | `src/view/` | ViewTransform, ViewFraming, ViewportChrome, ViewportUpdateHold | **done (2363)** |
| 16 | `src/slideshow/` (+fold) | ZoomBlur helpers, zoom-region gesture | **done (2364)** |
| 17 | `src/item/` | item components, frame geometry, handles, interact session, ItemWorld | **done (2365)** |
| — | `src/tilelod/` | unchanged | done |
| — | `src/` root | `imageview*`, `imageitem*`, `imageloader`, `pagepath`, `main.cpp`, shared bags, contentxform, placementlinear | stay until later |

## Rules

1. One domain per tip/commit stack; no behaviour changes.
2. Update `BILTOO_LIB_SOURCES` and test source lists.
3. Includes: `"session/foo.h"` from outside; inside a domain prefer the same
   prefix (tilelod style) so grep stays uniform.
4. Build green after the phase.

See also [RELEASE_0.2.0.md](RELEASE_0.2.0.md) §3a.
