# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.22-message-log` (base `d456ffc`).

### Message log
- `MessageLogWidget`: selectable monospace log, Copy/Clear, amber/red flash + badge.
- Text panel TTS status uses compact MessageLogWidget (not grey QLabel).
- **Messages** dock (bottom): TTS errors (auto-show) + OCR log lines.

### Apply
```bash
git pull --ff-only …/biltoo-2715.22-message-log-d456ffc.bundle HEAD
```
