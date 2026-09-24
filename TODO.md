# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2611.1-fix-flashhud-callers** (base `7d823d8`).

### This tip
`flashHud` was peeled to `hostHud().showFlash` but mode controllers still
called `m_view->flashHud`. Updated:

- WorkspaceController (reload / hard reload)
- ImageController (reload / hard reload)
- GalleryController (reload / hard reload)
- CropController (crop flash)

### Apply
```bash
git pull --ff-only …/biltoo-2611.1-fix-flashhud-callers-7d823d8.bundle HEAD
```
