# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2317-warm-embedded-preview.**

`cachedEmbeddedPreviewImage` + warmSessionOpenMemos prefer durable EMB
(EXIF / PDF /Thumb) over ThumbHash so second open still gets underlay after
ImageCache clear. Probe finish tags underlay fallback EMB vs LQIP by edge.

Includes 2313–2316. Requires **thumtoo-322**.

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-322.1-pdf-page-thumb-embedded-bd9cca0.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2317.1-warm-embedded-preview-999be36.bundle HEAD
```

Next: **2318**.
