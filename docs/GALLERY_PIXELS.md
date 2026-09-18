# Gallery pixels

**LQIP underlay + grid tiles.** No soft / HOST whole-frame underlay or SoftOnly jobs.

| Layer | When |
|-------|------|
| LQIP | Stand-in until tile plan covers; only sample installed from ImageCache |
| Tiles | `tileLodWanted` (~32px+ on-screen) — `TileLoadCoordinator` issues |
| Soft PreferCache / SoftOnly | **Not** used in Gallery (not for tile band, not for classic decode) |

## Paths

- `scheduleClassicImageDecode` in Gallery: probe + LQIP preview + `scheduleGalleryDecode` / tile tick
- Pass1: LQIP-only installs
- Pass2 / watchdog: skip soft for `tileLodWanted`
- `canAcceptDisplaySample`: rejects soft > LQIP when tiles own the cell
