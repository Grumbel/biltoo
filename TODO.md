# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2391-unused-sizegate-rebuild** (on top of `660c49c` stack).

Includes **2381–2390**.

### 2391
Remove unused `sizeGate` in `GalleryController::rebuildVirtualPlan` (-Wunused-variable).

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2391.1-unused-sizegate-rebuild-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2390
- [x] 2391 unused sizeGate warning
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
