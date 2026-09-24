# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2614.1-fix-link-stub-edge-sticky** (base `7d823d8`).

### This tip
Link / test fallout:
- Remove `tests/thumtoocache_appearance_stub.cpp` from **biltoo_lib** sources
  (duplicate symbols with real thumtoocache.cpp; stub stays on unit tests only)
- `edgeZoneAt` → `hostImage().edgeZoneAt` + EdgeNavPolicy::Zone
- `restoreStickyPanAnchor` → `hostImage().…` (framing lambdas + characterization)
- `enterGallery` in characterization → `hostGallery().enterGallery`

### Apply
```bash
git pull --ff-only …/biltoo-2614.1-fix-link-stub-edge-sticky-7d823d8.bundle HEAD
```
