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
| — | `src/tilelod/` | unchanged | done |
| — | `src/` root | `imageview*`, `imageitem*`, `imageloader`, `pagepath`, `main.cpp`, shared types (`placementlinear`, `itemframegeometry`, …) | stay until later |

## Phase 1 file list (`src/session/`)

- `sessiondocument.{h,cpp}`
- `sessionappearance.{h,cpp}`
- `sessionseedbook.{h,cpp}`
- `sessionexpand.{h,cpp}`
- `sessionexport.{h,cpp}`
- `sessionopen.{h,cpp}`
- `sessionsort.{h,cpp}`
- `sessionpathorder.h`
- `sessionreorderdialog.{h,cpp}`
- `sessionbindbook.h`
- `sessionchrome.h`
- `sessionloadgate.h`
- `packorderview.h`
- `packorderoverlay.h`

**Not in phase 1:** `cropsession`, `attentionsession`, `grouptransformsession`,
`textlayersession`, `pageguidesession`, `iteminteractsession`,
`mainwindow_session*`, `imageview_session*`.

## Rules

1. One domain per tip/commit stack; no behaviour changes.
2. Update `BILTOO_LIB_SOURCES` and test source lists.
3. Includes: `"session/foo.h"` from outside; inside `session/` prefer the same
   prefix (tilelod style) so grep stays uniform.
4. Build green after the phase.

See also [RELEASE_0.2.0.md](RELEASE_0.2.0.md) §3a.
