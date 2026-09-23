# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2399-size-gate-sweep-underlay** (on top of `660c49c` stack).

Includes **2381–2398**.

### Triple-check
| Claim | Status |
|-------|--------|
| Gate on for all packaged Gallery layouts | OK (`layoutDefersPopulateUntilSizes`) |
| Tiles blocked while gate active | OK (coordinator + scheduleGalleryDecode) |
| SizeReply underlay → ImageCache | OK (finishProbeSlot always put) |
| Gallery install EMB ≤320 | OK |
| Size-memo without underlay re-probes | OK (startIfNeeded 2398) |
| Progress sweep cannot close gate before underlay probe | **2399 fix** (was a race) |

**Next:** RC smoke cold + re-open same process.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2399.1-size-gate-sweep-underlay-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2398
- [x] 2399 progress sweep underlay race
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
