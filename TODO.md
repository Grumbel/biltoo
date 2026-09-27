# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.9-piper-server-flake` (base `d456ffc`).

### TTS / flake
- Flake input `text2sprech`; `biltoo` wraps `piper-server-full` on PATH +
  default voice (`TEXT2SPRECH_PIPER_MODELS`).
- `nix develop` / `biltoo-run` also get piper-server.
- **After pull:** `nix flake lock --update-input text2sprech` (lock not updated
  in this sandbox — no `nix` binary).

### Apply
```bash
git pull --ff-only …/biltoo-2716.9-piper-server-flake-d456ffc.bundle HEAD
nix flake lock --update-input text2sprech
```
