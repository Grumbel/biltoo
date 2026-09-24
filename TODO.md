# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2475.2-drop-unused-frame-size-helper** (base `7d823d8`).

### Fix
- Drop unused `itemHasReliableFrameSize` from `imageview_framing_image.cpp`
  (live copy is in `imagecontroller_framing.cpp` after ownership move)

### Apply
```bash
git pull --ff-only …/biltoo-2475.2-drop-unused-frame-size-helper-7d823d8.bundle HEAD
```
