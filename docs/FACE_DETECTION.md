<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Face detection

Optional **OpenCV YuNet** face detection, isolated under `src/face/`.

## Architecture

| Piece | Role |
|-------|------|
| `biltoo::face::FaceDetector` | Abstract backend (`detect(QImage)`). |
| `YunetFaceDetector` | OpenCV `FaceDetectorYN` when `BILTOO_HAVE_OPENCV=1` and a model is found. |
| `NullFaceDetector` | Build without OpenCV or missing model. |
| `FaceController` | Async run, last result, overlay flags; **no UI**. |
| `FacePanel` | Dock panel only; talks to `FaceController` via MainWindow. |

MainWindow owns `FaceController` and registers it on `ImageView` for scene-space overlay paint (`ViewShellChrome::paintForeground`).

Embeddings / recognition are **not** implemented yet; the same `FaceDetector` seam can host a future `FaceEmbedder` without changing the panel.

## Model

YuNet ONNX from [OpenCV Zoo](https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet)
(`face_detection_yunet_2023mar.onnx`).

Search order:

1. `BILTOO_FACE_YUNET_MODEL` (absolute path)
2. `$app/../share/biltoo/models/…`
3. XDG app data `…/models/…`
4. `data/models/face_detection_yunet_2023mar.onnx` (dev tree)
5. `/usr/share/biltoo/models/…`

## Build

```bash
# CMake finds OpenCV optionally
cmake -B build -S . -DTHUMTOO_SOURCE_DIR=…
# STATUS: biltoo: OpenCV face detection enabled (…)
```

Nix: `opencv` is a `buildInputs` entry in `default.nix`.

Without OpenCV, biltoo still builds; the panel shows the null backend message.

## UI

**Panels → Show Face Detection**: threshold, overlay / landmarks toggles, **Detect faces** on the current canvas image, result list, green boxes (+ red landmarks) on the image.
