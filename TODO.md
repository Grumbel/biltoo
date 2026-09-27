# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.6-tts-verify-notes` (base `d456ffc`).

### Text-to-speech Phase A — ready for local build
- Vendored speech stack; Text panel + Edit Speak/Stop; `--piper-socket`.
- Connect retry, owned-server lifecycle, sentence-splitter test, docs.
- Static + Python parity checks done; needs host compile + Speak smoke test.

### Apply
```bash
git pull --ff-only …/biltoo-2715.6-tts-verify-notes-d456ffc.bundle HEAD
```

### Next (when ready)
- Phase B: highlight regions for current sentence.
- Optional: flake PATH inject `piper-server` from text2sprech input.

### Prior
2715.5 stop/docs; 2715.4 splitter test; 2715.3 lifecycle; 2715.2 retry; 2715.1 Phase A.
