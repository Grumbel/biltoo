# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2889.4-face-switch-overlay (linear stack on `e345338`).

### 2889.4
- Clear face results on session index change; paint only when path/sessionId
  matches (no single-item fallback). Supersede in-flight detect; ImageLoader
  fallback when underlay has no pixels. Landmark radius scales with face size.

### 2889.3
- Flake `fetchurl` for YuNet ONNX; `BILTOO_FACE_YUNET_MODEL` in develop +
  qtWrapperArgs; install under `$out/share/biltoo/models/`.

### 2889.2
- `#include <opencv2/objdetect.hpp>` + CMake `objdetect` component.

### 2889.1
- Optional OpenCV YuNet face detection + Face Detection panel.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`. No parallel histories.
