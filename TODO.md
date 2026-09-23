# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2405-warm-gallery-restash** (base `d80d461`, includes 2403–2404).

### 2405 — Warm Gallery restash (no membership rebuild)
Gallery → Image → Gallery with an intact tile stash no longer runs
`populateGalleryCanvas` / `setWorkspacePaths`. Same `ImageItem*` cells return
(pixels + tile registry idle paths). Cold path (empty stash) still populates.

Verified: `TileLodRegistry::invalidateAll` is **session replace only**
(`invalidateSessionLoads` / `clearWorkspace`), not mode switch.

### 2404 — ImageCache memory budget
Default 384 MiB (`BILTOO_IMAGECACHE_MIB`); prefer keep underlays.

### 2403 — Size gate size-only
Gate settles on size book / process memo; underlay not required.

### Architecture direction (remaining)
- Optional underlay-only Store pass when size known and ImageCache cold.
- Qt materializes only; SessionDocument / ItemWorld / SessionImageId ground truth.

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2405.1-warm-gallery-restash-d80d461.bundle HEAD
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
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
