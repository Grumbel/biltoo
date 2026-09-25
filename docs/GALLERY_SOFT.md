<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery soft path — removed

Soft PreferCache / soft-ladder underlay for Gallery is **gone** and must not be
reintroduced. Placeholders are **LQIP** only; sharpness is **tiles**.

Host decode-window bookkeeping lives under the **GalleryDecode** name
(`gallerydecodesm`, `GalleryDecodeBook`, `GalleryDecodeState`) — not "soft".

See [GALLERY_PIXELS.md](GALLERY_PIXELS.md).

## Broader direction

Gallery is done. **Image / Workspace Soft** is still alive and should die the
same way (LQIP/EMB until tiles cover). See **TODO.md → Kill Soft — tiles
everywhere**. Not a 0.2.0 tag requirement.

