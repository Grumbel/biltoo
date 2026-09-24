# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2481.1-own-color-adjust-commit-timer** (base `7d823d8`).

### Ownership transfer
- **Colour-adjust commit QTimer** on ImageController (parented to ImageView)
- `scheduleColorAdjustCommit` arms controller timer; flush stays on ImageView

### Apply
```bash
git pull --ff-only …/biltoo-2481.1-own-color-adjust-commit-timer-7d823d8.bundle HEAD
```
