# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2371-src-archivepath-host.**

**Source layout phases 1–23 done** — domain subdirs complete for 0.2.0.

| Dir | Role |
|-----|------|
| `session/` `crop/` `gallery/` `shell/` `image/` | session, crop, gallery, chrome, Image mode |
| `display/` `host/` `tilelod/` | pixels, Store glue, tile LOD |
| `workspace/` `attention/` `slideshow/` | mode collaborators |
| `hud/` `text/` `color/` `view/` `item/` | HUD, text, grade, view geometry, item chrome |
| `src/` root | `imageview*`, `imageitem*`, loader, pagepath, contentxform, bags |

Static verify: 0 unprefixed domain includes; cmake OK except generated `version.h`.

**Next (release path):** RC smoke → pin thumtoo ≥323 → VERSION 0.2.0.

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2371.1-src-layout-continue-8d5061c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–23
- [ ] RC smoke (icons.qrc, Gallery, crop, export)
- [ ] VERSION 0.2.0 + tag
