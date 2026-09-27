# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.17-tts-panel-connect` (base `d456ffc`).

### Done (Phase A + controls + highlight)
- Piper TTS: Speak/Stop, multi-sentence, QAudioSink, duration advance.
- Text panel: Voice, Tempo, Vol 0–150%; speaking green highlight + progress.
- Panel TTS controls wired only via `connectTextPanel` (no duplicate slots).

### Apply
```bash
git pull --ff-only …/biltoo-2715.17-tts-panel-connect-d456ffc.bundle HEAD
```

### Optional later
- Persist voice/tempo/volume in settings.
- Multi-page speak with highlight following page changes.
