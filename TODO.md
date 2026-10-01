# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2890.3-sface-env-develop (linear stack on `e345338`).

### 2890.3
- Export `BILTOO_FACE_SFACE_MODEL` in nix develop shellHook, biltoo-run, and
  biltoo-run-gdb (fetchurl was already a flake input; env was missing).

### 2890.2
- Face ImageLoader off GUI thread.

### 2890.1
- SFace embeddings + gallery.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`.
