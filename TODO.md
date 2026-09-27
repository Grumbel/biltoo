# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.7-tts-synth-feedback` (base `d456ffc`).

### Text-to-speech Phase A
- Speak/Stop UI; connect retry; server lifecycle; splitter test.
- **2715.7:** Synthesizing… vs Speaking…; QMediaPlayer errors; 45s synth watchdog.

### Apply
```bash
git pull --ff-only …/biltoo-2715.7-tts-synth-feedback-d456ffc.bundle HEAD
```

### Debug if still silent
1. Status stuck on **Synthesizing…** → piper-server not returning WAV (voices / PATH / server log).
2. Status reaches **Speaking…** but no sound → Qt Multimedia / system audio output.
3. Status shows **Synthesis error** / **Media playback error** → read the message.

### Prior
2715.6 verify notes … 2715.1 Phase A.
