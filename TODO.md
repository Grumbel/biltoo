# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2807.1-gallery-annot-paint` (base `2085c07`).

### 2807.1
- Gallery / Workspace: paint committed annotations on every live tile that has
  annotation data (presentation only; tools stay Image-mode)
- paintItemAnnotations helper; item sid only (no session-cursor fallback)

### Prior (2806.x)
- Custom tool cursors + mutual exclusion + viewport cursor stickiness fixes

### Apply
```bash
git pull --ff-only …/biltoo-2807.1-gallery-annot-paint-2085c07.bundle HEAD
```
