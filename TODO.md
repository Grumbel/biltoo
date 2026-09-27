# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.9-tts-qaudi-sink` (base `d456ffc`).

### Text-to-speech
- Playback uses **QAudioSink** + WAV PCM parse (not QMediaPlayer).
- Fixes: no QtMultimedia backends / pipewire-0.3 missing under Nix.

### Apply
```bash
git pull --ff-only …/biltoo-2715.9-tts-qaudi-sink-d456ffc.bundle HEAD
```
