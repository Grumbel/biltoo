# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2889.2-yunet-objdetect-include (linear stack on `e345338`).

### 2889.2
- Fix YuNet build: `#include <opencv2/objdetect.hpp>` and CMake component
  `objdetect` (`FaceDetectorYN` is not in `dnn` alone).

### 2889.1
- Optional OpenCV **YuNet** face detection (`src/face/`), isolated from UI.
- **Panels → Face Detection**: detect current image, score threshold, overlay /
  landmarks, result list. Scene overlay via `FaceController::paintSceneOverlay`.
- Null backend when OpenCV or model missing. Docs: [docs/FACE_DETECTION.md](docs/FACE_DETECTION.md).
- Nix: `opencv` in `default.nix` buildInputs.

### 2888.1
- TTS speaking highlight without path-gate / wrong start page.

### 2887.1
- Open last session on empty CLI.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`. No parallel histories.
