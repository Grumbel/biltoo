# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2314-embedded-preview-underlay.**

Prefer SizeReply.embedded (EXIF JPEG) over ThumbHash LQIP for ImageCache
underlay on size probe / prepare_paths. Does not skip tile scheduling.

Includes **2313** (pathraster noteDelivery iterator fix).
Requires **thumtoo-321** (embedded preview on size path).

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-321.1-embedded-preview-exif-bd9cca0.bundle HEAD
# from 999be36 / origin with 2312:
git -C biltoo pull --ff-only …/biltoo-2314.2-embedded-preview-underlay-999be36.bundle HEAD
```

Next: **2315** (provenance stamp EMB vs LQIP; PDF /Thumb when thumtoo has it).
