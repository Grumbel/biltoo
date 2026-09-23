<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery display pixels

**LQIP underlay + grid tiles.** Soft PreferCache / soft ladder whole-frame
encode is **not** used in Gallery (removed).

| Layer | Role |
|-------|------|
| **LQIP** (≤96) | ThumbHash placeholder until tiles cover. |
| **EMB** (≤320) | EXIF / PDF `/Thumb` container preview (`SizeReply.embedded`); stamped **EMB** in debug overlay. Not tiles. |
| **Tiles** | Sharpness for cells with on-screen long edge > 32 px. Durable Store hits preferred; encode only when coverage missing. |

Filmstrip uses the same product rule via `scheduleFilmstripTilePixels` /
`scheduleTileSynthOrPyramid` (LQIP + TileSynth). Gallery never requests soft
PreferCache for underlay.

## Open path

See **[GALLERY_OPEN.md](GALLERY_OPEN.md)** for fences and phase order.

1. **Size gate** — `request_size` for the session (warm skip only if size +
   ImageCache underlay); tiles blocked until the set settles.
2. **Underlay** — SizeReply EMB/LQIP → ImageCache → `tryInstallGalleryUnderlay`
   and virtual-slot paint (never layout authority).
3. **Plan + virtual window** — definitive sizes only; no stand-in squares.
4. **Tiles** — after gate complete, TileLoadCoordinator for visible cells.
5. HUD shows resolving progress (`N / M sizes`, failures, rough ETA).
