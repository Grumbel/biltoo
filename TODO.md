# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2365-src-item-subdir.**

**Source layout phases 1–17 done** (domain subdirs largely complete for 0.2.0):

| Dir | Contents |
|-----|----------|
| `session/` `crop/` `gallery/` `shell/` | document, crop, gallery, chrome UI |
| `display/` | pipeline, surface, quality, path raster, tile load/prefetch |
| `workspace/` `attention/` `slideshow/` | mode collaborators |
| `hud/` `text/` `color/` | HUD, text layer, colour grade |
| `host/` | ThumtooCache + memos + size probe |
| `view/` | pure view transform/framing/viewport helpers |
| `item/` | item chrome/components/world (not `imageitem*`) |
| `tilelod/` | unchanged |
| `src/` root | `imageview*`, `imageitem*`, loader, pagepath, contentxform, placementlinear, bags |

Static verify: 0 unprefixed domain includes; cmake paths exist except generated `version.h`.

Next: **RC smoke** (open, Gallery, crop, export, shell icons); pin thumtoo ≥323; VERSION 0.2.0.

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2365.1-src-layout-continue-8d5061c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–17
- [ ] RC smoke (esp. icons.qrc after shell move; crop; export)
- [ ] VERSION 0.2.0 + tag
