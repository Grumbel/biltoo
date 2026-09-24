# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2460.1-fix-secondary-black-sizebook** (base `7d823d8`).

### Dual compare goal
Image-mode **side-by-side compare** of two session rows: shared ItemWorld +
DisplayPipeline; independent ←/→ on the focused pane; primary keeps session
cursor / filmstrip.

### Fix 2460 (secondary was black)
- `hostSizeBook()` uses shared ItemWorld size book when bound (secondary had
  empty local book → 1×1 layout / blank paint)
- Secondary always `ImageController::enter()` on open
- Copy soft provider + background brush from primary
- Defer seed open until after splitter layout (`QTimer::singleShot(0)`)

### Next
- Optional lock-step dual nav
- QTimer lifetime on host switch
- Confirm dual shows two images on host

### Apply
```bash
git pull --ff-only …/biltoo-2460.1-fix-secondary-black-sizebook-7d823d8.bundle HEAD
```
