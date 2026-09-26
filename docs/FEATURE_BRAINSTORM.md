<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Feature brainstorm (containers, codecs, alpha, faces, annotations)

Status: **ideas only** — not a commitment or implementation plan. Capture
trade-offs against biltoo + thumtoo as they exist today.

Related: [PERFORMANCE.md](PERFORMANCE.md), [PIXEL_PIPELINE_REDESIGN.md](PIXEL_PIPELINE_REDESIGN.md),
[THUMTOO_HOST_CONTRACT.md](THUMTOO_HOST_CONTRACT.md), [TEXT_TO_SPEECH.md](TEXT_TO_SPEECH.md),
[TEXT_OVERLAY.md](TEXT_OVERLAY.md), project/session docs.

---

## 1. Native biltoo package (tiles + text, “CBZ with a pyramid”)

### Idea

A first-class container (e.g. `.biltoo` / zip-based) that stores:

- page images **and** precomputed tile pyramids
- text / OCR layers (and later annotations)
- session metadata (order, ids, appearance)

so open is “map files + install,” not “decode + climb + OCR.”

### Fit today

- Session sources already include directories, **CBZ**, multi-page PDF/DjVu via thumtoo.
- Tiles and soft ladders are **derived** under thumtoo’s cache (`soft/`, tile grids),
  path-keyed, not shipped with the user’s archive.
- Project files store layout/appearance; they are **not** a pixel container.

### Design sketch

| Layer | Content |
|-------|---------|
| Manifest | page list, `SessionImageId`-stable ids, dimensions, codec, hash |
| Pages | original or normalized full raster (optional if tiles are complete) |
| Pyramid | per-page tile grid levels (same addressing as thumtoo tiles) |
| Text | `PageTextLayer` JSON/CBOR per page (native + OCR preference) |
| Optional | attention, crop recipes, grades as today in project/ECS |

Format options: **ZIP** (CBZ-like, easy inspect) vs **single-file** custom.
ZIP wins for tooling and partial read unless we need one mmap blob.

### Pros

- Cold open of large books without rebuilding pyramids.
- Portable “finished” reading package (tiles + text) across machines.
- Natural place for multi-page text selection / TTS corpora later.

### Cons / risks

- **Stale tiles** if the user replaces a page image without invalidating the pyramid.
- Size blow-up (pyramid ≈ 1.3–1.5× full raster depending on levels/codec).
- Two writers (biltoo export vs thumtoo cache) must share **one addressing schema**.
- Must not fork the host↔thumtoo contract — package is an alternate *source*,
  still delivering pixels through the same install path.

### Suggested path

1. Spec manifest + tile layout **aligned with thumtoo tile keys** (not a third grid).
2. Export “Pack for offline” from a warm session (tiles already in cache).
3. Open path: detect package → thumtoo or biltoo serves tiles without re-encode.
4. Keep plain CBZ/PDF as import; package is an *acceleration / distribution* format.

---

## 2. JPEG XL progressive / partial decode (“zoomable”)

### Idea

Use JXL’s progressive / region decode so zoom/pan loads only needed bytes
instead of a discrete tile pyramid.

### Reality check

- biltoo/thumtoo already use **JXL for soft ladder** and discuss durable soft
  levels; **tiles** are a separate grid assembled for PreferCache / viewport.
- JXL can do progressive and, with care, **decode of subsets**, but:
  - API and threading behaviour differ from “independent JPEG tiles.”
  - Random access to an arbitrary ROI is **not** as simple as reading tile `i,j`
    from a directory of small files.
  - Full multi-res **tile pyramid in JXL** is still research/product surface
    area; most apps keep an external pyramid or whole-frame decode + downsample.

### When JXL helps biltoo

| Use | Verdict |
|-----|---------|
| Soft / overview stills | **Already on this path** — keep tuning |
| Replace entire tile system with one progressive JXL per page | **High risk** — fight PreferCache, tile LOD, archive members |
| Optional “single JXL page” source that progressively refines | **Interesting experiment** behind a feature flag |
| Package format page payload | Lossy JXL or JPEG tiles both fine; pick by bench |

### Recommendation

Do **not** block the native package or tile UX on “JXL is the pyramid.”
Treat progressive JXL as an **optional decode backend** for full/soft frames;
keep explicit tiles as the viewport workhorse until a prototype beats tile
assemble on real CBZ/PDF sessions.

---

## 3. WebP (or any codec) for tiles — “just be fast”

### Idea

Faster encode/decode for tile blobs; codec is secondary to throughput.

### Fit

[PERFORMANCE.md](PERFORMANCE.md): tile assemble and GUI install cost often
dominate; codec is one term. Soft path already cares about encode cost when
filling the ladder.

### Guidance

- **Benchmark the whole path**: encode tile → store → decode → upload/blit → paint.
- Candidates: JPEG (libjpeg-turbo), WebP, JXL lossy, PNG only where alpha needed.
- For **opaque** page tiles, JPEG or WebP lossy at quality tuned for text/comics
  is enough; switching codecs without measuring install rate is noise.
- Keep **one** durable tile codec per cache schema version; migrations are painful.

### Recommendation

Add a microbench target (thumtoo or biltoo tools) for “N tiles round-trip”
before changing defaults. WebP is a fine candidate; it is not automatically
the winner.

---

## 4. RGBA / transparency

### Idea

Support images with alpha (UI assets, scanned overlays, WebP/PNG with alpha).

### Fit today

- Much of the pipeline assumes **opaque** document pages (paper + ink).
- Premultiplied ARGB appears in slideshow / effects; not the full gallery/image
  path contract.
- Canvas backgrounds and checker patterns exist for “what sits behind the page.”

### Gaps to verify (tests, not assumptions)

- Decode of PNG/WebP with alpha into host cache
- Tile encode **preserving** alpha (PNG/WebP lossless or residual alpha plane)
- Gallery soft and PreferCache not forcing opaque RGB
- Crop/grade/content bake compositing correctly on transparent edges
- Export CBZ/PNG preserving alpha

### Recommendation

1. Explicit **characterization tests** (transparent PNG/WebP fixture through
   open → gallery → image → crop → export).
2. Document “opaque page” vs “alpha asset” modes if behaviour must diverge.
3. Only then extend tile codec choice for alpha pages (likely PNG/WebP, not JPEG).

---

## 5. Face recognition (dlib or similar)

### Idea

Detect faces for attention points, smart crop, or search.

### Fit

- **Attention** already stores points on items (`AttentionController` / components).
- “Detect attention” can remain generic interest points; faces are one detector.

### Options

| Library | Notes |
|---------|--------|
| dlib | Classic, heavy, good enough face HOG/CNN; packaging weight |
| OpenCV | If already in graph for other reasons |
| Lightweight CNN (onnxruntime) | Modern, model files on disk, more moving parts |
| External tool | Offline batch to write attention points into project |

### Design principles

- Run **off GUI thread** (worker / thumtoo-side job); never in paint.
- Write results into existing **Attention** (or a new sparse component), not a
  parallel face database in v1.
- Privacy: local only; no cloud API.
- Optional dependency — biltoo must run without face models installed.

### Recommendation

Backlog feature: “Detect faces → attention points” behind a dependency flag.
Do not couple to the tile/container work.

---

## 6. Annotation layer (vector draw / ink)

### Idea

User-drawn vectors and text on top of the page (review, teaching, redaction marks).

### Fit

- Text regions are **from the file/OCR**, not freehand ink.
- Content transforms (crop, orient, grade) are appearance; annotations should
  be a **separate layer** in document or scene space.
- ECS/ItemWorld is the right place for durable per-`SessionImageId` annotation
  documents if we want undo and project save.

### Sketch

```
Page image (pixels)
  └─ Text layer (regions)          — existing
  └─ Annotation layer (vectors)    — new: strokes, shapes, text boxes
  └─ Attention / crop chrome       — tools, not permanent art
```

- Store annotations in **page/image coordinates** (content space), not view
  pixels, so zoom/pan/crop stay consistent ([CONTENT_COORDINATES.md](CONTENT_COORDINATES.md)).
- Tools: pen, highlighter, rectangle, arrow, text; Select tool might later
  hit-test annotation objects vs text regions (mode or sub-tool).
- Export: flatten optional; package format (§1) can store annotation JSON.

### Recommendation

Separate epic from TTS and containers. Minimal vertical slice: stroke list +
paint in `drawForeground` + undo + project persistence. Vector library (e.g.
simple own path structs first; Qt paths) before full Inkscape-class editing.

---

## Priority lens (opinionated)

| Idea | Priority signal | Why |
|------|-----------------|-----|
| Native package with tiles + text | Medium–high research | Matches “slow cold open” pain; must align thumtoo schema |
| JXL as progressive pyramid | Low near-term | High uncertainty; tiles already solve viewport |
| Tile codec bench (WebP etc.) | Low–medium | Cheap to measure; change only with data |
| RGBA characterization | Medium | Correctness; discover real bugs before features |
| Face → attention | Low | Nice; optional heavy dep |
| Annotation layer | Medium long-term | Distinct product surface; coordinate with tools epic |

---

## See also

- Pixel ladder and tiles: [PERFORMANCE.md](PERFORMANCE.md), [PIXEL_PIPELINE_REDESIGN.md](PIXEL_PIPELINE_REDESIGN.md)
- Text overlay: [TEXT_OVERLAY.md](TEXT_OVERLAY.md)
- TTS: [TEXT_TO_SPEECH.md](TEXT_TO_SPEECH.md)
- Tools / selection: `TODO.md` (2712.x tool work)
