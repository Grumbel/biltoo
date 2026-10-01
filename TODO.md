# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2885.1-coveralone-single-layout (linear stack on `e345338`).

### 2885.1
- Fix Double view + Binding: Cover alone when membership is a single page
  (cover): `applySpreadLayout` no longer early-outs at N&lt;2. Non-members are
  pruned and `layoutSpread` places the cover (works with the equal-height
  centre/scale pose from upstream). Previously the cover stayed at identity
  while the prior pair remained → three pages visible.
  Docs: [docs/SPREAD.md](docs/SPREAD.md).

### 2884.2
- Fix: `SessionDocument` private members for gallery layout mode/columns
  (methods were added without the fields → compile error).

### 2884.1
- Gallery layout mode and columns in session + project JSON.

### 2883.1 / 2882.1 / 2881.*
- TTS document speak; spread equal height; pipewire; Gallery↔Image doc.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`. No parallel histories.
