# Gallery pixels

**LQIP underlay + grid tiles.** No soft / HOST whole-frame underlay.

| Layer | When |
|-------|------|
| LQIP | Stand-in until tile plan covers (or tiny cells never in tile band) |
| Tiles | `tileLodWanted` (~32px+ on-screen cell) — `TileLoadCoordinator` issues |
| Soft PreferCache | **Not** used for tile-band cells |

Pass1 installs LQIP from ImageCache only. Pass2 does not schedule soft for
`tileLodWanted` paths. Paint rejects soft samples under tiles.
