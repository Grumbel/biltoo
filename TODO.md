# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.13-selection-tts-harden` (base `d456ffc`).

### Verified / fixed
- Text panel: no setCurrentIndex on hover; dedupe selection/hover signals.
- TTS: QAudioSink; advance when near end (processedUSecs / duration), not on early Idle.

### Apply
```bash
git pull --ff-only …/biltoo-2715.13-selection-tts-harden-d456ffc.bundle HEAD
```
