# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2612.1-fix-release-sticky-zoom-self** (base `7d823d8`).

### This tip
`ImageController` still called `m_view->releaseStickyZoom()` after that API
left ImageView. Use `releaseStickyZoom()` on self (zoom region release +
framing free-zoom paths).

### Apply
```bash
git pull --ff-only …/biltoo-2612.1-fix-release-sticky-zoom-self-7d823d8.bundle HEAD
```
