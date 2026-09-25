# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2701.1-gallery-pose-soft-reload` (base `932ed5c`).

### 2701.1 — Gallery wrong scale / soft F5 no-op
Image mode sets item scale to 1 (view-owned fit/1:1). Warm Gallery stash
restore skipped re-pack → cells stayed at scale 1. Layout button was a no-op
while the size gate was active (ExplicitLayout blocked). Soft F5 kept ImageCache
LQIP + settled-ladder marks so cells stayed Placeholder 16px (need 512px);
only Shift-F5 (hard) cleared those.

Fix:
- Stash restore: rebuildVirtualPlan + syncVirtualWindow (re-apply pack poses)
- Allow ExplicitLayout during size gate
- Soft F5: ImageCache::remove + forgetPixelsSettled + pose re-apply

### 2700.1 — Sticky zoom survives Gallery
### 2699.1 — New session clears "Loading tiles…"
### 2698.1 — Gallery size probe vs pack scale race

### Apply
```bash
git pull --ff-only …/biltoo-2701.1-gallery-pose-soft-reload-932ed5c.bundle HEAD
```
