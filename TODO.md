# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2315-embedded-underlay-emb-stamp.**

ImageCache debug overlay: force tag **EMB** for EXIF/PDF /Thumb underlay,
**LQIP** for ThumbHash. Requires **thumtoo-322** (EXIF + PDF /Thumb embedded).

Includes 2313 (pathraster iterator) and 2314 (embedded underlay install).

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-322.1-pdf-page-thumb-embedded-bd9cca0.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2315.1-embedded-underlay-emb-stamp-999be36.bundle HEAD
```

Next: **2316**.
