# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2377-src-util-subdir.**

**Source layout complete for 0.2.0** (phases 1–31).

Root is façade-only: `imageview*`, `imageitem*`, `imageview_types.h`, `main.cpp`.
All other translation units live under domain dirs (17 domains + tilelod).

Static verify: 0 unprefixed domain includes; cmake OK except generated `version.h`.

**Next (release path — stop layout moves):**
1. RC smoke (open, Gallery, crop, export, shell icons after `.qrc` move)
2. Pin thumtoo ≥ 323 in flake.lock
3. VERSION 0.2.0 + tag

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2377.1-src-layout-complete-8d5061c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–31 (layout complete)
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
