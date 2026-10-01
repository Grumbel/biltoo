# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2881.2-pipewire-qt-multimedia (on `e345338` + this stack).

### 2881.2
- Silence Qt Multimedia `Couldn't load pipewire-0.3` under Nix: add `pipewire`
  buildInput + `LD_LIBRARY_PATH` via `qtWrapperArgs`, `nix develop` shellHook,
  and `biltoo-run` / `biltoo-run-gdb`. Documented in
  [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md).

### 2881.1
- [docs/GALLERY_IMAGE_MODE_SWITCH.md](docs/GALLERY_IMAGE_MODE_SWITCH.md) —
  Gallery ↔ Image latency analysis, measurement plan, architecture options.
- **Next (latency):** capture one round-trip with
  `BILTOO_GUI_BUDGET_LOG=1 BILTOO_MODE_DEBUG=1` before structural changes.

### Prior
- 2880.1 AnnotationSession::ensurePage QHash warning
- 2879.1 toolbar / tile fail
- 2878.1 dock/toolbar chrome

### Bundle policy
Work-line base: `e345338` (upstream tip at start of this stack). Full stack in
each tip bundle.
