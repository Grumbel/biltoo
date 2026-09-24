# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2483.1-own-hud-chrome** (base `7d823d8`).

### Ownership transfer
- **HudChrome** owns HudAppearance, HudFlash, flash QTimer
- ImageView: thin flashHud / hostHudPrefs / hostHudFlash

### Apply
```bash
git pull --ff-only …/biltoo-2483.1-own-hud-chrome-7d823d8.bundle HEAD
```
