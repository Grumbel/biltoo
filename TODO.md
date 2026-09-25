# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2657.1-menu-shortcut-polish` (base `9740316`).

### Done
- Batch targets through 2656.2.
- **2657.1 Menu / shortcut / enablement polish**
  - File: Export Session Images first; page PNG/PDF grouped after separator.
  - Image → Document submenu (EPUB / PDF Embedded; experimental tip).
  - Gallery → Layout submenu (grouped pack modes).
  - Edit: clipboard vs session separators.
  - Tips for letter-key chords (R/C/H/Q/Space); Esc on Back; Fit Ctrl+Shift+F;
    Workspace Ctrl+Shift+W.
  - Shortcuts dialog intro (viewer chords + Esc chain).
  - Enablement: page print/export Workspace-only; session export needs files;
    slideshow actions disabled in Workspace.

### Verify
- Host build + open File/Image/Gallery menus; Esc from Image; slideshow grey in Workspace.

### Apply
```bash
git pull --ff-only …/biltoo-2657.1-menu-shortcut-polish-9740316.bundle HEAD
```
