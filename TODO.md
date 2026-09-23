# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2356-src-slideshow-subdir.**

**Source layout phases 1–8 done**:

| Dir | Contents |
|-----|----------|
| `src/session/` | document, appearance, expand/export/open/sort/reorder, pack order |
| `src/crop/` | controller, session, geometry, command, handles |
| `src/gallery/` | controller, layout, pack, size-resolve, decode SM |
| `src/shell/` | mainwindow*, icons, panels, prefs |
| `src/display/` | imagecache, pipeline, surface, quality, path raster |
| `src/workspace/` | controller, geometry/nav, background dialog, group transform, page guide, stack |
| `src/attention/` | controller, session, geometry |
| `src/slideshow/` | controller, clocks, phase/atlas/motion policies, settings, motion-scroll |
| `src/tilelod/` | unchanged |
| `src/` | imageview*, imageitem*, loader, hud, text, coloradjust, shared geometry, … |

Includes: `session/…` `crop/…` `gallery/…` `shell/…` `display/…` `workspace/…` `attention/…` `slideshow/…`.
Static verify: 0 unprefixed includes of moved headers.

Next candidates: `hud/`, `text/`, or leave façades and do **RC smoke** + VERSION 0.2.0.

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2356.1-src-attention-slideshow-8d5061c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–5
- [x] src/workspace phase 6
- [x] src/attention phase 7
- [x] src/slideshow phase 8
- [ ] RC smoke (esp. icons.qrc after shell move; crop; export)
- [ ] VERSION 0.2.0 + tag
