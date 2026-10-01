// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/facedetector.h"

namespace biltoo::face {
namespace {

class NullFaceDetector final : public FaceDetector {
public:
    FaceDetectorInfo info() const override
    {
        FaceDetectorInfo i;
        i.id = QStringLiteral("null");
        i.displayName = QStringLiteral("No face detector");
#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV
        i.available = false;
        i.detail = QStringLiteral("OpenCV is linked but no YuNet model was found.");
#else
        i.available = false;
        i.detail = QStringLiteral("Built without OpenCV (BILTOO_HAVE_OPENCV=0).");
#endif
        return i;
    }

    FaceDetectionResult detect(const QImage &image, float /*scoreThreshold*/) const override
    {
        FaceDetectionResult r;
        r.imageSize = image.size();
        r.backendId = QStringLiteral("null");
        r.error = info().detail;
        return r;
    }
};

} // namespace

std::unique_ptr<FaceDetector> FaceDetector::createNullDetector()
{
    return std::make_unique<NullFaceDetector>();
}

} // namespace biltoo::face
