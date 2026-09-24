# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2462.1-fix-dual-shortcut-and-secondary-paint** (base `7d823d8`).

### Fixes
- **Shortcut:** Dual compare is `Ctrl+Shift+2` (was `Ctrl+Shift+D`, clashed with Open Directory)
- **Secondary paint:** software viewport (avoids second QOpenGLWidget under splitter)
- **Soft seed:** warm ImageCache from filmstrip/LQIP before/during open
- **Retry:** 50ms + 100ms re-open if still no display pixels

### Dual model
- Shared ItemWorld + size book
- Per-surface DisplayPipeline
- Primary keeps OpenGL; secondary uses QWidget viewport

### Apply
```bash
git pull --ff-only …/biltoo-2462.1-fix-dual-shortcut-and-secondary-paint-7d823d8.bundle HEAD
```
