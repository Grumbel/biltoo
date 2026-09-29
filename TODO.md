# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.8-gallery-open-paint-freeze (base 2085c07).

### 2815.8
- Gallery→Image: freeze viewport updates during leaveForImageMode (stash + enter)
- Defer finishCurrentIndexChromeUpdate (filmstrip/title) to next event-loop tick
- Cuts the noticeable ~100ms empty-scene paint lag before first Image frame

### Prior
- 2815.7 tile LOD settle
- 2815.6 Performance panel
- 2815.5 vips concurrency 1

### Apply
```bash
git pull --ff-only …/biltoo-2815.8-gallery-open-paint-freeze-2085c07.bundle HEAD
```

Fast-forward from 104158c1 (2815.7).
