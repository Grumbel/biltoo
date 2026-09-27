# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.3-tts-server-lifecycle` (base `d456ffc`).

### Text-to-speech Phase A (hardened)
- Vendored text2sprech speech stack under `src/speech/`.
- Text panel Speak/Stop; Edit menu; Ctrl+Shift+S / Ctrl+.
- Lazy spawn; `--piper-socket`; connect retry ~6s; owned-server reconnect/respawn.
- Speak selection or full page (reading order); no region highlight yet (Phase B).

### Apply
```bash
git pull --ff-only …/biltoo-2715.3-tts-server-lifecycle-d456ffc.bundle HEAD
```

### Prior
2715.2 connect retry; 2715.1 Phase A; 2714.20 Prepare Tile Cache UI.
