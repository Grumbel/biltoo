# Host pixel cache unification (session plan)

## Problem
Multiple in-process pixel stores disagree:

| Store | Key | What it holds |
|-------|-----|----------------|
| `ImageCache` | path | Best sample ≤2048 (authority) |
| `m_previewByPath` | — | **Removed** (use ImageCache) |
| `m_ssRasterByPath` | path | Slideshow samples |
| `ImageItem::m_source` | item | Oriented full or soft display |

Switching to slideshow reads soft / 512 from ImageCache even when Image mode
already holds a multi-megapixel `m_source`.

## Target (no new features)
1. **One host path→raster map** for undecoded-appearance samples: `ImageCache`.
2. Store up to **display ladder max (2048)**, not preview 512. Thumtoo remains
   durable compressed source of truth; ImageCache is process RAM.
3. **Upward-only replace** by long edge (already true).
4. Slideshow **reads ImageCache first**; `m_ssRasterByPath` is a thin hot set
   that always mirrors puts into ImageCache (then can shrink later).
5. Pure helpers for edge clamp / adequacy (data-driven constants).

## Non-goals this session
- thumtoo schema changes
- Gallery soft scheduler rewrite
- Moving SessionDocument out of ImageView
- New HUD / UX features

## Steps
A. ImageCache API: max store edge, clamp helper, document contract
B. Stop downscaling full loads to 512; put ladder-capped rasters
C. installDisplayPixels: put **raw** pixels into ImageCache before orient
D. putSlideshowRaster always ImageCache::put; slideshow read path prefers cache
E. Docs + TODO handoff; small readable commits

## Risk
Memory: 2048² RGBA × many paths. Mitigate: clamp to 2048, keep entry cap 384,
evict on insert (existing). Prefer thumtoo for cold paths.
