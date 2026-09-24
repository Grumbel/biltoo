# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2486.1-own-slideshow-progress-timer** (base `7d823d8`).

### Fixes / ownership
- HudChrome: no QWidget include (viewport update via callback) — fixes build
- **Slideshow progress QTimer** owned by SlideshowController

### Apply
```bash
git pull --ff-only …/biltoo-2486.1-own-slideshow-progress-timer-7d823d8.bundle HEAD
```
