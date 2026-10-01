<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Face detection and recognition

Optional **OpenCV YuNet** detection + **SFace** embeddings, isolated under `src/face/`.

## Architecture

| Piece | Role |
|-------|------|
| `FaceDetector` | Abstract detect (`YuNet` / null). |
| `FaceEmbedder` | Abstract embed + cosine match (`SFace` / null). |
| `FaceGallery` | Local identities (JSON under AppData). |
| `FaceController` | Async detect → embed → match; enroll; overlay. |
| `FacePanel` | Dock UI only. |

`model_id` is stored with each embedding so a future backend swap does not
silently compare incompatible vectors.

## Models (Nix flake)

| Env | File |
|-----|------|
| `BILTOO_FACE_YUNET_MODEL` | `face_detection_yunet_2023mar.onnx` |
| `BILTOO_FACE_SFACE_MODEL` | `face_recognition_sface_2021dec.onnx` |

Both are `fetchurl` fixed-output inputs; wrappers and `nix develop` set the env
vars; packages install under `$out/share/biltoo/models/`.

## Recognition flow

1. **Detect / recognize** loads the session path, runs YuNet, embeds each face
   with SFace (`alignCrop` + `feature`), matches the gallery (cosine ≥ threshold,
   default **0.363**).
2. **Enroll**: select a face in the list, type a name, **Enroll selected face**.
3. Overlay draws the match label when present.

Gallery file: `$XDG_DATA_HOME/…/face_gallery.json` (via `AppDataLocation`).

## Build

OpenCV optional (`BILTOO_HAVE_OPENCV`). Without models, detect/embed backends
report unavailable; the rest of biltoo still runs.
