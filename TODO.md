# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2796.1-fs-toolbar-edge-hud` (base `a989daf`).

### 2796.1
- Leave fullscreen: restore left Tools toolbar in all modes (not only Workspace)
- Edge-nav HUD (prev/next/gallery chevrons): hidden while annotation tool active
  (same as crop/attention); clear hover when tool engages

### Prior
- 2795 toolbar separators
- 2794–2783 annotation / docs

### Apply
```bash
git pull --ff-only …/biltoo-2796.1-fs-toolbar-edge-hud-a989daf.bundle HEAD
```

### Deferred
- PDF source write-back — docs/PDF_SOURCE_WRITEBACK.md
