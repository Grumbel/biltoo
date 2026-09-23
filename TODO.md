# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2353-src-display-subdir.**

**Source layout phases 1–5 done** (verified):

| Dir | Contents |
|-----|----------|
| `src/session/` | document, appearance, expand/export/open/sort/reorder, pack order |
| `src/crop/` | controller, session, geometry, command, handles |
| `src/gallery/` | controller, layout, pack, size-resolve, decode SM |
| `src/shell/` | mainwindow*, icons, panels, prefs |
| `src/display/` | imagecache, pipeline, surface, quality, path raster |
| `src/tilelod/` | unchanged |
| `src/` | imageview*, imageitem*, loader, workspace, attention, … |

Includes: `session/…`, `crop/…`, `gallery/…`, `shell/…`, `display/…`.
Static verify: 0 unprefixed includes of moved headers; all domain `.cpp` in CMake.

Next: **RC smoke** (open, Gallery, crop, export, shell icons); then VERSION 0.2.0.

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2353.1-src-display-subdir-2f201f6.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–5
- [ ] RC smoke (esp. icons.qrc after shell move; crop; export)
- [ ] VERSION 0.2.0 + tag
