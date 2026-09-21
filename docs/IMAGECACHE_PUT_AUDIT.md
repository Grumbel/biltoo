<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ImageCache::put audit (host-raw contract)

Contract (`imagecache.h`): values are **unoriented host** samples. Content
bake is only via `installDisplayPixels` / `materializeDisplay`.

| Site | Source | Verdict |
|------|--------|---------|
| `installDisplayPixels` | incoming claimed host-raw | OK (callers must not pass baked) |
| `createItemFromImage` | worker decode host | OK |
| `loadSoftPreviewPixels` / pool soft jobs | disk LQIP/soft | OK if load path does not bake |
| `gallerycontroller` LQIP downscale of host | host scale | OK |
| `completeLoad` / quality jobs | disk | OK |
| `cropcontroller` decoded | crop source host | OK if full decode |
| `slideshow` raster delivery | phase host | OK if host |
| `export` load | disk | OK |
| `thumbnailbar` sampleForImageModePending | filmstrip icons | **2176** puts removed |
| `thumbnailbar` makeThumbnail | applies XDG for **cell paint only** | must not put (does not) |

Rule: never `ImageCache::put(path, item->displayImage())` when applied
ContentXform is set.
