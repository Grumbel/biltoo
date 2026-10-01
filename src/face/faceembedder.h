// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BILTOO_FACE_FACEEMBEDDER_H
#define BILTOO_FACE_FACEEMBEDDER_H

#include "face/facetypes.h"

#include <QImage>
#include <memory>

namespace biltoo::face {

/**
 * Abstract face embedder (recognition features). Worker-thread safe for
 * embed()/match(); construct on the GUI thread.
 *
 * OpenCV SFace is the default when BILTOO_HAVE_OPENCV and a model is found.
 */
class FaceEmbedder {
public:
    virtual ~FaceEmbedder() = default;

    virtual FaceBackendInfo info() const = 0;
    virtual int embeddingDim() const = 0;

    /**
     * Embed one face. @p image is the same sample used for detection;
     * @p face must include a valid rect (landmarks improve alignment when present).
     * Returns empty embedding on failure.
     */
    virtual QVector<float> embed(const QImage &image, const FaceBox &face) const = 0;

    /** Cosine similarity in [−1, 1] (SFace); higher means more similar. */
    virtual float matchCosine(const QVector<float> &a, const QVector<float> &b) const = 0;

    static std::unique_ptr<FaceEmbedder> createDefaultEmbedder();
    static std::unique_ptr<FaceEmbedder> createNullEmbedder();
};

} // namespace biltoo::face

#endif
