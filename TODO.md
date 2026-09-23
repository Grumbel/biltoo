# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2404-imagecache-memory-budget** (base `d80d461`, includes 2403).

### 2404 — ImageCache memory budget
- Eviction is total approximate ARGB32 KiB (default **384 MiB**), not entry count.
- Prefer drop large samples before EMB/LQIP underlays (mode-switch friendly).
- Override: `BILTOO_IMAGECACHE_MIB`. Hard entry ceiling 8192 as safety only.
- Docs: `PIXEL_HOST_CACHE.md`, `ENVIRONMENT.md`.

### 2403 — Size gate size-only
Gallery → Image → Gallery no longer re-arms full size probe when sizes are known
but underlay is cold. Gate = size book / process memo only.

### Architecture direction (remaining)
- **Tiles:** host tile sessions already have `BILTOO_TILE_RAM_MIB` +
  `BILTOO_TILE_MAX_IDLE`; verify mode switch does not needlessly
  `invalidateAll`.
- Qt only materializes; SessionDocument / ItemWorld / SessionImageId stay ground
  truth (IDENTITY.md).
- Optional: underlay-only Store pass when size known and ImageCache cold (no
  size-gate involvement).

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2404.1-imagecache-memory-budget-d80d461.bundle HEAD
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
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
