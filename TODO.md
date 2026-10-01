# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2890.2-face-load-off-gui (linear stack on `e345338`).

### 2890.2
- Face detect must not call ImageLoader on the GUI thread (ASSERT). Load path
  inside FaceController worker; GUI may only pass underlay QImage.

### 2890.1
- SFace embeddings + gallery enroll/match.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`.
