# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2354-src-workspace-subdir.**

**Source layout phases 1–6 done** (verified):

| Dir | Contents |
|-----|----------|
| `src/session/` | document, appearance, expand/export/open/sort/reorder, pack order |
| `src/crop/` | controller, session, geometry, command, handles |
| `src/gallery/` | controller, layout, pack, size-resolve, decode SM |
| `src/shell/` | mainwindow*, icons, panels, prefs |
| `src/display/` | imagecache, pipeline, surface, quality, path raster |
| `src/workspace/` | controller, geometry/nav, background dialog, group transform, page guide, stack |
| `src/tilelod/` | unchanged |
| `src/` | imageview*, imageitem*, loader, attention, slideshow, shared geometry, … |

Includes: `session/…`, `crop/…`, `gallery/…`, `shell/…`, `display/…`, `workspace/…`.
Static verify: 0 unprefixed includes of moved headers; all domain `.cpp` in CMake.

Left at root (shared): `placementlinear`, `itemframegeometry`, `imageview_*`, attention, slideshow, …

Next: further domain moves if useful (attention, slideshow, …) or **RC smoke** (open, Gallery, crop, export, shell icons); then VERSION 0.2.0.

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2354.1-src-workspace-subdir-8d5061c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–5
- [x] src/workspace phase 6
- [ ] RC smoke (esp. icons.qrc after shell move; crop; export)
- [ ] VERSION 0.2.0 + tag
