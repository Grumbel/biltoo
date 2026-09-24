# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2633.1-gallery-lqip-store-seed** (base `08fe2d1`).

### This tip — Gallery LQIP missing despite tile cache
LQIP/EMB underlay was only seeded from SizeReply → ImageCache. After tile
prepare, Store has LQIP but process ImageCache often does not (warm size memo
skips request_size when something is cached, or size-only warm). Gallery cells
stayed blank until tiles painted — and cells ≤32px screen never request tiles.

- `cachedLqipImage`: worker may read Store get_embedded / get_lqip
- `scheduleStoreUnderlaySeed`: async seed + sizeReady for GUI install
- `tryInstallGalleryUnderlay`: on miss, schedule Store seed
- `putEmbeddedOrLqipUnderlay`: only skip when underlay-band sample exists

### Apply
```bash
git pull --ff-only …/biltoo-2633.1-gallery-lqip-store-seed-08fe2d1.bundle HEAD
```
