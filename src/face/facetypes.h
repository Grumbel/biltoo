// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BILTOO_FACE_FACETYPES_H
#define BILTOO_FACE_FACETYPES_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace biltoo::face {

/** One detected face in image-pixel coordinates (origin top-left of the sample). */
struct FaceBox {
    QRectF rect;           ///< Bounding box in pixels of the analysed image.
    float score = 0.f;     ///< Detector confidence in [0, 1].
    /** Five landmarks when available: right eye, left eye, nose, mouth right, mouth left. */
    QVector<QPointF> landmarks;
};

/** Result of a detection run (path is the session/image path that was analysed). */
struct FaceDetectionResult {
    QString path;
    QSize imageSize; ///< Size of the sample that was analysed (for mapping).
    QVector<FaceBox> faces;
    QString backendId; ///< e.g. "yunet", "none".
    QString error;     ///< Non-empty if the run failed.
};

/** Capability / identity of a FaceDetector implementation. */
struct FaceDetectorInfo {
    QString id;          ///< Stable id: "yunet", "null".
    QString displayName;
    bool available = false;
    QString detail;      ///< Model path, missing dep message, etc.
};

} // namespace biltoo::face

#endif
