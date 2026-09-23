# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2383-probe-cancel-races-orient-plan** (on top of `660c49c` stack).

Includes **2381–2382**.

### 2383 fixes (review of 2381–2382)
1. Stale `finishProbeSlot` no longer removes a re-enqueued path from
   `g_probeQueued` after `cancelSizeProbes`.
2. `preparePaths` respects `sizeProbeGeneration` (no ImageCache/`sizeReady`
   after session replace).
3. Virtual plan uses orient-aware `contentLayoutSize(..., false)` without Store.

**Thumtoo pinned** in `flake.lock` → `f71d183` (thumtoo-323).

**Next (release path):**
1. RC smoke (session switch mid-resolve; rotated gallery pack; cold open sizes)
2. VERSION 0.2.0 + tag

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2383.1-probe-cancel-races-orient-plan-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–31
- [x] pin thumtoo ≥ 323
- [x] Resolving sizes… HUD top-left
- [x] 2381 unused provCell
- [x] 2382 cancel size probes; sizes before layout
- [x] 2383 probe cancel races; preparePaths epoch; orient plan
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag

## Backlog (0.3.0)
See prior TODO: size-resolve throughput, failure diagnostics, ImageView ownership.
