// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/faceembedder.h"

namespace biltoo::face {
namespace {

class NullFaceEmbedder final : public FaceEmbedder {
public:
    FaceBackendInfo info() const override
    {
        FaceBackendInfo i;
        i.id = QStringLiteral("null");
        i.displayName = QStringLiteral("No face embedder");
#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV
        i.detail = QStringLiteral("OpenCV is linked but no SFace model was found.");
#else
        i.detail = QStringLiteral("Built without OpenCV (BILTOO_HAVE_OPENCV=0).");
#endif
        i.available = false;
        return i;
    }

    int embeddingDim() const override { return 0; }

    QVector<float> embed(const QImage &, const FaceBox &) const override { return {}; }

    float matchCosine(const QVector<float> &, const QVector<float> &) const override
    {
        return 0.f;
    }
};

} // namespace

std::unique_ptr<FaceEmbedder> FaceEmbedder::createNullEmbedder()
{
    return std::make_unique<NullFaceEmbedder>();
}

} // namespace biltoo::face
