# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2407-sparse-size-plan** (base `d80d461`, includes 2403–2406).

### 2407 — Sparse progressive size plan
**Bug:** Gallery showed ~dozens of cells then stalled until the whole size gate
finished. `rebuildVirtualPlan` used an **ordered prefix** (`break` at first
unresolved path). SizeReply is out of order → one hole blocked every later
definitive size from the plan.

**Fix:** Skip unresolved holes; include every definitive/failed row. Fill
layouts (`layoutNeedsAllSizes`) still wait for the full set. Docs: GALLERY_OPEN.

### Stack
| Tip | What |
|-----|------|
| 2403–2406 | Size gate / ImageCache / warm restash |
| 2407 | Sparse virtual plan during size gate |

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2407.1-sparse-size-plan-d80d461.bundle HEAD
```

## Prior (2026-09-23)

**Tip: biltoo-2402-underlay-path-complete** (on top of `660c49c` stack).

Includes **2381–2401**.

### Verification pass (2402)
Found and fixed residual underlay installs outside `tryInstallGalleryUnderlay`.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2402.1-underlay-path-complete-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2401
- [x] 2402 underlay path completeness
- [x] 2403 size gate size-only (mode switch no full re-probe)
- [x] 2404 ImageCache memory budget
- [x] 2405 warm Gallery restash (skip populate on return)
- [x] 2406 warm restash verification (enterGalleryMode + membership)
- [x] 2407 sparse size plan (skip holes during gate)
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
