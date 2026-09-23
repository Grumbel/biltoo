# Source tree layout (0.2.0)

Flat `src/` → domain subdirectories. Pattern matches `src/tilelod/`:
`#include "session/sessiondocument.h"` with `target_include_directories(… src)`.

## Phases

| Phase | Directory | Move | Status |
|-------|-----------|------|--------|
| 1 | `src/session/` | Session document, appearance, expand/export/open/sort/reorder, pack order | **done (2349)** |
| 2 | `src/crop/` | crop controller/session/geometry/command | **done (2350)** |
| 3 | `src/gallery/` | gallery controller, layout, pack, size-resolve, decode SM | **done (2351)** |
| 4 | `src/shell/` | mainwindow*, icons, help, prefs/panels | **done (2352)** |
| 5 | `src/display/` | imagecache, pipeline, quality, surface, path raster | **done (2353)** |
| 6 | `src/workspace/` | workspace controller, geometry, group transform, page guide, stack | **done (2354)** |
| 7 | `src/attention/` | attention controller, session, geometry | **done (2355)** |
| 8 | `src/slideshow/` | controller, clocks, policies, settings, motion-scroll | **done (2356)** |
| 9 | `src/hud/` | hud model, appearance, flash, geometry | **done (2357)** |
| 10 | `src/text/` | text layer geometry/session, search policy | **done (2358)** |
| 11 | `src/shell/` (+fold) | toc, thumbnail bar, shortcuts, EPUB layout | **done (2359)** |
| 12 | `src/color/` | coloradjust pipeline + commit bag | **done (2360)** |
| 13 | `src/host/` | ThumtooCache, process memos, size probe | **done (2361)** |
| 14 | `src/display/` (+fold) | TileLoadCoordinator, TileNeighborPrefetch | **done (2362)** |
| 15 | `src/view/` | ViewTransform, ViewFraming, ViewportChrome, ViewportUpdateHold | **done (2363)** |
| 16 | `src/slideshow/` (+fold) | ZoomBlur helpers, zoom-region gesture | **done (2364)** |
| 17 | `src/item/` | item components, frame, handles, ItemWorld | **done (2365)** |
| 18 | `src/session/` (+fold) | projectfile (.biltoo JSON) | **done (2366)** |
| 19 | `src/shell/` (+fold) | filmstrip geometry | **done (2367)** |
| 20 | `src/image/` | ImageController, EdgeNavPolicy | **done (2368)** |
| 21 | `src/view/` (+fold) | canvas background/pattern geometry | **done (2369)** |
| 22 | `src/item/` (+fold) | PlacementLinear, SelectionGeometry | **done (2370)** |
| 23 | `src/host/` (+fold) | ArchivePath | **done (2371)** |
| — | `src/tilelod/` | unchanged | done |
| — | `src/` root | `imageview*`, `imageitem*`, `imageloader`, `pagepath`, `main.cpp`, contentxform, shared bags | intentional façade |

## Rules

1. One domain per commit; no behaviour changes.
2. Update `BILTOO_LIB_SOURCES` and test source lists.
3. Includes: `"domain/foo.h"` consistently (tilelod style).
4. Build green after each phase.

See also [RELEASE_0.2.0.md](RELEASE_0.2.0.md) §3a.
