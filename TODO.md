# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2882.1-spread-equal-height-center (linear stack on `e345338`).

### 2882.1
- Double view / spread: place pages at slot **centre** (ImageItem is
  centre-origin) and always apply height-match scale so unequal pages sit
  side-by-side at equal height. See `ImageController::applySpreadLayout` and
  [docs/SPREAD.md](docs/SPREAD.md) §4.2.

### 2881.2
- Silence Qt Multimedia `Couldn't load pipewire-0.3` under Nix: `pipewire`
  buildInput + `LD_LIBRARY_PATH` via `qtWrapperArgs`, `nix develop` shellHook,
  and `biltoo-run` / `biltoo-run-gdb`. [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md).

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
Work-line base: `e345338`. **No parallel histories.** Each tip bundle is
`e345338..HEAD` (full stack). New tip supersedes previous tip bundle files.
