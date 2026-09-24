# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2634.1-gallery-scroll-restore** (base `3ae7a41`).

### This tip — Gallery→Image→Gallery forgot scroll
applyPendingRestore cleared m_haveViewCenter immediately after the first
centerOn. returnToGallery then refreshed scrollbar geometry and singleShot
reassert became a no-op — overview jumped to origin / wrong place.

- Keep leave camera flags until ExplicitLayout (or EnterGallery with no camera)
- Pack must not centerOn(0,0) while leave camera is armed
- Warm stash enter reasserts after sceneRect is restored
- Pack end reasserts leave camera even when pendingRestore already false

### Apply
```bash
git pull --ff-only …/biltoo-2634.1-gallery-scroll-restore-3ae7a41.bundle HEAD
```
