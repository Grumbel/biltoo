# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2485.1-own-hud-status-refresh-timer** (base `7d823d8`).

### Ownership transfer
- **Status-refresh coalesce QTimer** on HudChrome
- ImageView::refreshStatus thin arm via m_hud.scheduleStatusRefresh
- GalleryController still owns its separate gallery status-refresh timer

### Apply
```bash
git pull --ff-only …/biltoo-2485.1-own-hud-status-refresh-timer-7d823d8.bundle HEAD
```
