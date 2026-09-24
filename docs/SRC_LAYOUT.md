# Source tree layout (0.2.0)

Domain subdirectories under `src/`. Includes use the domain prefix
(`#include "session/sessiondocument.h"`) with `target_include_directories(… src)`.

## Domain map

| Directory | Contents |
|-----------|----------|
| `session/` | document, appearance, expand/export/open/sort/reorder, projectfile |
| `crop/` | controller, session, geometry, command, handles |
| `gallery/` | controller, layout, pack, size-resolve, decode SM, layout bags |
| `shell/` | mainwindow*, icons, panels, prefs, toc, filmstrip, shortcuts, epub |
| `image/` | ImageController, EdgeNavPolicy, ToolPolicy |
| `display/` | imagecache, pipeline, surface, quality, path raster, tile load/prefetch, LoadGeneration |
| `host/` | ThumtooCache, memos, size probe, ArchivePath, PagePath, ImageLoader |
| `workspace/` | controller, geometry/nav, background, group transform, page guide, stack |
| `attention/` | controller, session, geometry |
| `slideshow/` | controller, clocks, policies, settings, motion-scroll, zoom-blur |
| `hud/` | model, appearance, flash, geometry |
| `text/` | layer geometry/session, search policy |
| `color/` | coloradjust pipeline + commit bag |
| `content/` | ContentXform |
| `view/` | ViewTransform, framing, viewport, canvas background/pattern |
| `item/` | components, frame, handles, ItemWorld, placement/selection, size/state books |
| `util/` | logging, thread helpers, PerfStats, TTFP trace |
| `tilelod/` | tile LOD subsystem (pre-existing) |
| `src/` root | **façade only:** `imageview.{h,cpp}`, `imageitem*`, `imageview_types.h`, `main.cpp` |
| `*/imageview_routers.cpp` | ImageView public method bodies co-located with domain ownership (image, crop, text, workspace, view, display, session, hud, shell) |

## Rules

1. One domain per commit; no behaviour changes on moves.
2. Update `BILTOO_LIB_SOURCES` and test source lists.
3. Prefer prefixed includes inside and outside the domain.
4. Do **not** dump `imageview_*.cpp` into a folder without ownership transfer.

Phases 1–31: see git history `Refactor: move/fold …` (2349–2377).

See also [RELEASE_0.2.0.md](RELEASE_0.2.0.md) §3a.
