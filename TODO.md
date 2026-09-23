# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2398-size-gate-underlay-warm** (on top of `660c49c` stack).

Includes **2381–2397**.

### Double-check (2397 + 2398)
Verified:
- layoutDefersPopulateUntilSizes: all non-FreeForm → true (gate on)
- TileLoadCoordinator::tick returns while gate active
- updateDecodeWindow: LQIP-only while gate active
- scheduleGalleryDecode: tiles only when !active; EMB band ≤320 for underlay
- finishProbeSlot always ImageCache::put non-null SizeReply underlay
- **2398:** size memo without ImageCache no longer skips request_size / closes gate

**Next:** RC smoke cold + second open same process (ImageCache clear + size memo).

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2398.1-size-gate-underlay-warm-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2397
- [x] 2398 size-gate underlay warm requirement
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
