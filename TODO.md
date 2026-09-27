# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.2-tts-connect-retry` (base `d456ffc`).

### Text-to-speech Phase A (+ connect retry)
- Retry piper-server connect on connectionError (~6s), matching text2sprech.

### Text-to-speech Phase A
- Vendored text2sprech speech client/server manager/playback/splitter under `src/speech/`.
- Text panel Speak/Stop; Edit menu; shortcuts Ctrl+Shift+S / Ctrl+..
- Lazy piper-server spawn; `--piper-socket` for external server.
- Speak selection or full page (reading order); no region highlight yet (Phase B).

### Apply
```bash
git pull --ff-only …/biltoo-2715.2-tts-connect-retry-d456ffc.bundle HEAD
```

### Prior
2714.20 Prepare Tile Cache UI explain.
