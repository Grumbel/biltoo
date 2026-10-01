# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2889.5-face-stable-detect (linear stack on `e345338`).

### 2889.5
- Detect always loads the **session path** at the current index (not underlay).
- YuNet: continuous RGB→BGR Mat, downscale long edge ≤1280, map boxes back,
  reject non-finite / out-of-image boxes; mutex around shared FaceDetectorYN.

### 2889.4
- Clear overlay on index change; strict path/session paint; landmark size.

### 2889.3
- Flake-fetch YuNet ONNX.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`.
