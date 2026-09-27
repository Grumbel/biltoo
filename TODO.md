# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.15-tts-pause-timer` (base `d456ffc`).

### Text-to-speech Phase A (runtime-hardened)
- QAudioSink WAV playback (no QMediaPlayer backend required).
- Multi-sentence: **duration timer** advances queue; pause/resume re-arms timer.
- Text panel: hover does not clear multi-select; signal dedupe.
- UI: Speak/Stop, Edit menu, `--piper-socket`, connect retry, server lifecycle.
- Test: `ctest -R sentence-splitter`.

### Apply
```bash
git pull --ff-only …/biltoo-2715.15-tts-pause-timer-d456ffc.bundle HEAD
```

### Still needs host check
- Multi-sentence Speak → Finished.
- Text panel multi-select while hovering list.
