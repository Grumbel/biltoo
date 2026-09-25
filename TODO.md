# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2700.1-sticky-zoom-survives-gallery` (base `932ed5c`).

### 2700.1 — Sticky zoom survives Gallery
`enterGallery` called `releaseStickyZoom()`, wiping Fit/Fill/1:1 preference.
Left/right in Image kept sticky; Image → Gallery → other image did not.

Sticky *applies* only in Image (`applyImageModeFraming`); the preference is
mode-independent and must not be cleared on Gallery enter or Gallery Ctrl+wheel.

### 2699.1 — New session clears "Loading tiles…"
### 2698.1 — Gallery size probe vs pack scale race
### 2697.1 — drop kTileOverlap symbol
### 2696 — exact tile math only

### Apply
```bash
git pull --ff-only …/biltoo-2700.1-sticky-zoom-survives-gallery-932ed5c.bundle HEAD
```
