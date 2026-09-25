# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2669.1-hard-reload-forget-size` (base `2e49220`).

### Stack
… crop/tile defensive maps (2665–2668) …
**2669.1** — Shift+F5 forgets session ImageSizeBook entry so probe can re-apply size

### Crop / tiles (user diagnosis)
Squished PDF after crop was **stale Store tiles**, fixed by Shift+F5 — not a
PDF-only paint bug. Kept 2665–2668 as defensive (soft UV underlay +
orientedCropRect for paint/viewport); they match materializeDisplay and do not
change identity when cropSourceSize already matches native.

### Shift+F5 gap
purgePathDurable cleared ProcessMemos size + Store tiles, but **session
ImageSizeBook** kept the old definitive size. Item intrinsic and pack aspect
stayed wrong until process restart. Hard reload now `ImageSizeBook::take`s the
path before re-probe / LoadReplace (Image, Gallery, Workspace).

### Apply
```bash
git pull --ff-only …/biltoo-2669.1-hard-reload-forget-size-2e49220.bundle HEAD
```
