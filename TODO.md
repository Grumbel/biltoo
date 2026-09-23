# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2408-warm-restash-no-repack** (base `d80d461`, includes 2403–2407).

### 2408 — Warm restash: no EnterGallery repack
Gallery→Image→Gallery still felt like a reload: `enter()` always ran
`applyLayout(EnterGallery)` after stash restore (full pack + decode window +
tile re-issue).

**Fix:** Keep stashed pack poses; only `updateDecodeWindow()` for blanks.
Rematerialize only outdated content appearance → optional `ContentChange` pack.

### 2407 — Sparse progressive size plan
Skip unresolved holes in virtual plan (not ordered prefix).

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2408.1-warm-restash-no-repack-d80d461.bundle HEAD
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
- [x] 2408 warm restash no EnterGallery repack
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
