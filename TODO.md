# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2890.4-face-always-path-load (linear stack on `e345338`).

### 2890.4
- Never use canvas underlay for face detect; always ImageLoader path on worker.
  Fixes "only first image works" when underlay still holds previous pixels.

### 2890.3
- Export BILTOO_FACE_SFACE_MODEL in develop / biltoo-run.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`.
