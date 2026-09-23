# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2360-src-color-subdir.**

**Source layout phases 1–12 done**:

| Dir | Contents |
|-----|----------|
| `src/session/` | document, appearance, expand/export/open/sort/reorder, pack order |
| `src/crop/` | controller, session, geometry, command, handles |
| `src/gallery/` | controller, layout, pack, size-resolve, decode SM |
| `src/shell/` | mainwindow*, icons, panels, prefs, toc, filmstrip bar, shortcuts, epub layout |
| `src/display/` | imagecache, pipeline, surface, quality, path raster |
| `src/workspace/` | controller, geometry/nav, background, group transform, page guide, stack |
| `src/attention/` | controller, session, geometry |
| `src/slideshow/` | controller, clocks, policies, settings, motion-scroll |
| `src/hud/` | model, appearance, flash, geometry |
| `src/text/` | layer geometry/session, search policy |
| `src/color/` | coloradjust pipeline + commit bag |
| `src/tilelod/` | unchanged |
| `src/` | imageview*, imageitem*, loader, thumtoo host, shared geometry, … |

Static verify: 0 unprefixed includes of moved headers; cmake paths exist (except generated `version.h`).

Next: **RC smoke** (open, Gallery, crop, export, shell icons); optional further folds (`item/`, thumtoo host glue); VERSION 0.2.0.

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2360.1-src-layout-continue-8d5061c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–12 (incl. shell fold + color)
- [ ] RC smoke (esp. icons.qrc after shell move; crop; export)
- [ ] VERSION 0.2.0 + tag
