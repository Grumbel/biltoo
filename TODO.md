# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2878.1-dock-toolbar-chrome (on `ea477d6` + agent stack).

### 2878.1
- **Docks on unmaximize:** `changeEvent` only runs fullscreen chrome when
  `WindowFullScreen` actually toggles (not on maximize/restore). Fixes docks
  closing via leave-fullscreen restore snapshots.
- **Toolbar:** slideshow nav centred (stretch | prev/play/next | stretch);
  zoom + **fullscreen** button on the right; group gaps are empty space (no
  separator line).

### Prior
- 2877.1 overlay device text
- 2876.1 overlay text size

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
