// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BILTOO_FACE_FACETYPES_H
#define BILTOO_FACE_FACETYPES_H

#include "imageview_types.h"

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

    /** Filled after embedding (optional). */
    QVector<float> embedding;
    QString embeddingModelId;

    /** Best gallery match after recognition (optional). */
    QString matchLabel;
    QString matchId;
    float matchScore = 0.f; ///< Cosine similarity (SFace); higher = closer.
};

/** Result of a detection run (path is the session/image path that was analysed). */
struct FaceDetectionResult {
    QString path;
    SessionImageId sessionId = kInvalidSessionImageId;
    QSize imageSize; ///< Size of the sample that was analysed (for mapping).
    QVector<FaceBox> faces;
    QString backendId; ///< e.g. "yunet", "none".
    QString embedBackendId; ///< e.g. "sface", "none".
    QString error;     ///< Non-empty if the run failed.
};

/** Capability / identity of a FaceDetector / FaceEmbedder implementation. */
struct FaceBackendInfo {
    QString id;          ///< Stable id: "yunet", "sface", "null".
    QString displayName;
    bool available = false;
    QString detail;      ///< Model path, missing dep message, etc.
};

/** @deprecated alias kept for existing detector headers. */
using FaceDetectorInfo = FaceBackendInfo;

/** One enrolled identity in the local gallery. */
struct FaceIdentity {
    QString id;    ///< Stable UUID.
    QString label; ///< User-facing name.
    QVector<float> embedding;
    QString embeddingModelId;
};

} // namespace biltoo::face

#endif
