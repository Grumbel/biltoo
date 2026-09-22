# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2316-embedded-underlay-band.**

Gallery underlay band raised to **kEmbeddedUnderlayMaxEdge (320)** so EXIF /
PDF `/Thumb` samples are not forced through the 96px LQIP clamp. Downscale for
install only — **never** put a smaller stand-in back into ImageCache (that
wiped EMB).

Includes 2313–2315. Requires **thumtoo-322**.

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-322.1-pdf-page-thumb-embedded-bd9cca0.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2316.1-embedded-underlay-band-999be36.bundle HEAD
```

Next: **2317**.
