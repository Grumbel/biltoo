# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2757.1-doubleview-menu-messages-pref` (base `9395b3e`).

### Done this tip
- View → **Double view** submenu: toggle, binding, direction, N, dual compare
- Messages panel: `messageLogVisible` (default false); applied after
  `restoreState`; persisted on visibility change / writeSettings; no auto-show
  on TTS errors

### Prior
- 2756.1: slideshow tile wake
- 2755.1 / 2754.1: pkg-config noise

### Apply
```bash
git pull --ff-only …/biltoo-2757.1-doubleview-menu-messages-pref-9395b3e.bundle HEAD
```
