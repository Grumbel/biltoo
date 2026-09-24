# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2629.1-gallery-centre-vs-scrollbar** (base `78739ac`).

### This tip — Gallery return still off-centre by scrollbar gutter; relayout snaps back
- Root cause (follow-up to 2628): `reassertViewport` preferred **scrollbar pixel**
  values over the **scene centre** snapshot. Bar AlwaysOn↔AsNeeded (and one vs
  two bars) changes viewport size between leave and return → pack a bar-width off.
- Deferred `restoreViewport` in `returnToGallery` **re-armed** `pendingRestore`,
  so an ExplicitLayout that looked correct was snapped back by the leave snapshot.
- Fix: prefer `centerOn(m_viewCenter)`; ExplicitLayout/EnterGallery clears
  restore state; singleShot only `reassertViewport` (no re-arm); refresh bar
  geometry after PackViewportGuard restore before reassert.

### Apply
```bash
git pull --ff-only …/biltoo-2629.1-gallery-centre-vs-scrollbar-78739ac.bundle HEAD
```
