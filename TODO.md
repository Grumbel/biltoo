# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2403-size-gate-size-only** (base `d80d461`).

### Problem
Gallery → Image → Gallery re-ran the full size gate (“Resolving sizes…” for the
whole session) even when sizes were already known.

**Cause:** `GallerySizeResolve::startIfNeeded` required definitive size **and**
`ImageCache::has(path)` underlay. Mode switches keep the size book / process
memos, but underlays may be cold or LRU-evicted → every path re-enqueued for
`request_size`.

### Fix
Size gate settles on size alone (host book or process memo). Underlay remains
opportunistic on SizeReply / PreferCache; missing underlay → dark chrome, not a
gate re-arm. Docs: `docs/GALLERY_OPEN.md`.

### Architecture direction (not done this tip)
Ground truth must not live on `QGraphicsItem` / `QGraphicsView`:
- **Sizes:** process memos + thumtoo Store (already); ImageSizeBook is session
  mirror — must not clear on mode switch (already only on session replace).
- **Host pixels:** `ImageCache` is process-lifetime LRU (entry-capped today;
  should become **memory-capped**).
- **Tiles:** durable in thumtoo; host tile sessions should survive mode switch.
- Qt only materializes; session/appearance stay on SessionDocument / ItemWorld /
  SessionImageId (IDENTITY.md).

Next product steps after this tip: RC smoke; optional memory-cap for ImageCache;
probe path that fills underlay without blocking the size gate.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2403.1-size-gate-size-only-d80d461.bundle HEAD
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
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
