# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2632.1-hud-quality-status** (base `0d34dd8`).

### This tip — HUD quality line
Status was `show Npx · need Mpx · have Npx` (plus a duplicated edge). After
tiles-everywhere, show≡have often; jargon was meaningless in the status bar.

- Gallery under-need: `Preview · 128px (need 512px)`
- Gallery covered: tier only
- Image partial: `High quality · 1200px of 4000px`
- Multi-item assembler no longer appends `(Npx)` when quality already has `px`

### Apply
```bash
git pull --ff-only …/biltoo-2632.1-hud-quality-status-0d34dd8.bundle HEAD
```
