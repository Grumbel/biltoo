<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery soft path — removed

Soft PreferCache / soft-ladder underlay for Gallery is **gone** and must not be
reintroduced. Placeholders are **LQIP** (and EMB) only; sharpness is **tiles**.

Host decode-window bookkeeping lives under the **GalleryDecode** name
(`gallerydecodesm`, `GalleryDecodeBook`, `GalleryDecodeState`) — not "soft".

See [GALLERY_PIXELS.md](GALLERY_PIXELS.md).

## Broader direction

Gallery is done. **Soft is product-dead for all modes** — see
[KILL_SOFT.md](KILL_SOFT.md). Image / Workspace must use EMB/LQIP underlay +
tiles (same as Gallery). Climb policy is `TileDisplay` / `EscalateToFull`, not
a soft ladder.
