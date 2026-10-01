// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/facedetector.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cmath>
#include <mutex>

#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
// FaceDetectorYN lives in objdetect (OpenCV ≥ 4.5.3), not dnn alone.
#include <opencv2/objdetect.hpp>
#endif

namespace biltoo::face {
namespace {

QStringList yunetSearchPaths()
{
    QStringList paths;
    if (const QByteArray env = qgetenv("BILTOO_FACE_YUNET_MODEL"); !env.isEmpty()) {
        paths.append(QString::fromLocal8Bit(env));
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    paths.append(appDir + QStringLiteral("/../share/biltoo/models/face_detection_yunet_2023mar.onnx"));
    paths.append(appDir + QStringLiteral("/models/face_detection_yunet_2023mar.onnx"));
    const QStringList dataRoots = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &root : dataRoots) {
        paths.append(root + QStringLiteral("/models/face_detection_yunet_2023mar.onnx"));
    }
    paths.append(QStringLiteral("data/models/face_detection_yunet_2023mar.onnx"));
    paths.append(QStringLiteral("/usr/share/biltoo/models/face_detection_yunet_2023mar.onnx"));
    return paths;
}

QString resolveYunetModelPath()
{
    for (const QString &p : yunetSearchPaths()) {
        if (QFileInfo::exists(p) && QFileInfo(p).isFile()) {
            return QFileInfo(p).absoluteFilePath();
        }
    }
    return {};
}

#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV

/** Longest edge for the detector input — full-res multi-MP images produce
 *  unstable YuNet boxes (huge / negative widths) on some OpenCV builds. */
constexpr int kYunetMaxEdge = 1280;

bool finiteBox(float x, float y, float w, float h, float score)
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(w) && std::isfinite(h)
        && std::isfinite(score) && w >= 1.f && h >= 1.f && score >= 0.f && score <= 1.01f;
}

class YunetFaceDetector final : public FaceDetector {
public:
    explicit YunetFaceDetector(QString modelPath)
        : m_modelPath(std::move(modelPath))
    {
    }

    FaceDetectorInfo info() const override
    {
        FaceDetectorInfo i;
        i.id = QStringLiteral("yunet");
        i.displayName = QStringLiteral("OpenCV YuNet");
        i.available = !m_modelPath.isEmpty();
        i.detail = m_modelPath.isEmpty()
            ? QStringLiteral("YuNet ONNX model not found (set BILTOO_FACE_YUNET_MODEL).")
            : m_modelPath;
        return i;
    }

    FaceDetectionResult detect(const QImage &image, float scoreThreshold) const override
    {
        FaceDetectionResult r;
        r.backendId = QStringLiteral("yunet");
        r.imageSize = image.size();
        if (image.isNull() || image.width() < 2 || image.height() < 2 || m_modelPath.isEmpty()) {
            r.error = image.isNull()
                ? QStringLiteral("Empty image for face detection.")
                : info().detail;
            return r;
        }

        // Deep RGB888 copy so bits() is tightly owned for the Mat lifetime.
        QImage rgb = image.convertToFormat(QImage::Format_RGB888);
        if (rgb.isNull()) {
            r.error = QStringLiteral("Could not convert image for face detection.");
            return r;
        }
        rgb = rgb.copy();

        cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3,
                       const_cast<uchar *>(rgb.constBits()),
                       static_cast<size_t>(rgb.bytesPerLine()));
        cv::Mat bgr;
        cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
        if (!bgr.isContinuous()) {
            bgr = bgr.clone();
        }

        // Detect on a bounded-size copy; map boxes back to original pixels.
        cv::Mat input = bgr;
        double invScale = 1.0;
        const int longEdge = std::max(bgr.cols, bgr.rows);
        if (longEdge > kYunetMaxEdge) {
            const double scale = double(kYunetMaxEdge) / double(longEdge);
            invScale = 1.0 / scale;
            cv::Mat resized;
            cv::resize(bgr, resized, cv::Size(), scale, scale, cv::INTER_AREA);
            input = resized;
        }

        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_yn) {
                m_yn = cv::FaceDetectorYN::create(
                    m_modelPath.toStdString(),
                    std::string(),
                    cv::Size(input.cols, input.rows),
                    scoreThreshold,
                    0.3f,
                    5000);
            }
            if (!m_yn) {
                r.error = QStringLiteral("FaceDetectorYN::create failed.");
                return r;
            }
            m_yn->setInputSize(cv::Size(input.cols, input.rows));
            m_yn->setScoreThreshold(scoreThreshold);
            m_yn->setNMSThreshold(0.3f);

            cv::Mat faces;
            m_yn->detect(input, faces);
            if (faces.empty()) {
                return r;
            }

            const qreal imgW = qreal(r.imageSize.width());
            const qreal imgH = qreal(r.imageSize.height());
            for (int i = 0; i < faces.rows; ++i) {
                const float x = faces.at<float>(i, 0);
                const float y = faces.at<float>(i, 1);
                const float w = faces.at<float>(i, 2);
                const float h = faces.at<float>(i, 3);
                const float score = faces.at<float>(i, 14);
                if (!finiteBox(x, y, w, h, score)) {
                    continue;
                }

                FaceBox box;
                box.rect = QRectF(qreal(x) * invScale, qreal(y) * invScale,
                                  qreal(w) * invScale, qreal(h) * invScale);
                // Drop boxes that do not intersect the image at all.
                if (!box.rect.intersects(QRectF(0, 0, imgW, imgH))) {
                    continue;
                }
                box.rect = box.rect.intersected(QRectF(0, 0, imgW, imgH));
                if (box.rect.width() < 1.0 || box.rect.height() < 1.0) {
                    continue;
                }
                box.score = score;
                box.landmarks.reserve(5);
                for (int k = 0; k < 5; ++k) {
                    const float lx = faces.at<float>(i, 4 + k * 2);
                    const float ly = faces.at<float>(i, 5 + k * 2);
                    if (!std::isfinite(lx) || !std::isfinite(ly)) {
                        continue;
                    }
                    box.landmarks.append(QPointF(qreal(lx) * invScale, qreal(ly) * invScale));
                }
                r.faces.append(box);
            }
        } catch (const cv::Exception &ex) {
            r.error = QString::fromUtf8(ex.what());
        } catch (...) {
            r.error = QStringLiteral("Unknown OpenCV error during face detection.");
        }
        return r;
    }

private:
    QString m_modelPath;
    mutable std::mutex m_mutex;
    mutable cv::Ptr<cv::FaceDetectorYN> m_yn;
};

#endif // BILTOO_HAVE_OPENCV

} // namespace

std::unique_ptr<FaceDetector> FaceDetector::createDefaultDetector()
{
#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV
    const QString model = resolveYunetModelPath();
    if (!model.isEmpty()) {
        return std::make_unique<YunetFaceDetector>(model);
    }
#endif
    return createNullDetector();
}

} // namespace biltoo::face
