// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BILTOO_FACE_FACEDETECTOR_H
#define BILTOO_FACE_FACEDETECTOR_H

#include "face/facetypes.h"

#include <QImage>
#include <memory>

namespace biltoo::face {

/**
 * Abstract face detector. Implementations must be safe to call from a worker
 * thread (no UI). Construct on the GUI thread; detect() may run off-thread.
 *
 * Backends are selected at runtime via createDefaultDetector() so biltoo can
 * build without OpenCV (null backend) and swap models later without touching UI.
 */
class FaceDetector {
public:
    virtual ~FaceDetector() = default;

    virtual FaceDetectorInfo info() const = 0;

    /**
     * Detect faces in @p image (any format; implementation converts as needed).
     * @p scoreThreshold drops low-confidence boxes (0–1).
     */
    virtual FaceDetectionResult detect(const QImage &image, float scoreThreshold) const = 0;

    /**
     * Preferred factory: OpenCV YuNet when BILTOO_HAVE_OPENCV and a model is
     * found; otherwise a null detector that reports unavailable.
     */
    static std::unique_ptr<FaceDetector> createDefaultDetector();

    /** Explicit null backend (always unavailable). */
    static std::unique_ptr<FaceDetector> createNullDetector();
};

} // namespace biltoo::face

#endif
