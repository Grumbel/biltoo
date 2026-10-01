// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/facedetector.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

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
    // Next to the running binary / share layout.
    const QString appDir = QCoreApplication::applicationDirPath();
    paths.append(appDir + QStringLiteral("/../share/biltoo/models/face_detection_yunet_2023mar.onnx"));
    paths.append(appDir + QStringLiteral("/models/face_detection_yunet_2023mar.onnx"));
    // Source-tree / data/ during development.
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
        if (image.isNull() || m_modelPath.isEmpty()) {
            r.error = info().detail;
            return r;
        }

        QImage rgb = image;
        if (rgb.format() != QImage::Format_RGB888 && rgb.format() != QImage::Format_RGBA8888
            && rgb.format() != QImage::Format_RGB32 && rgb.format() != QImage::Format_ARGB32) {
            rgb = image.convertToFormat(QImage::Format_RGB888);
        } else if (rgb.format() != QImage::Format_RGB888) {
            rgb = image.convertToFormat(QImage::Format_RGB888);
        }
        if (rgb.isNull()) {
            r.error = QStringLiteral("Could not convert image for face detection.");
            return r;
        }

        // OpenCV expects BGR contiguous.
        cv::Mat bgr(rgb.height(), rgb.width(), CV_8UC3);
        for (int y = 0; y < rgb.height(); ++y) {
            const uchar *src = rgb.constScanLine(y);
            cv::Vec3b *dst = bgr.ptr<cv::Vec3b>(y);
            for (int x = 0; x < rgb.width(); ++x) {
                dst[x][0] = src[x * 3 + 2];
                dst[x][1] = src[x * 3 + 1];
                dst[x][2] = src[x * 3 + 0];
            }
        }

        try {
            auto detector = cv::FaceDetectorYN::create(
                m_modelPath.toStdString(),
                std::string(),
                cv::Size(bgr.cols, bgr.rows),
                scoreThreshold,
                0.3f,
                5000);
            if (!detector) {
                r.error = QStringLiteral("FaceDetectorYN::create failed.");
                return r;
            }
            cv::Mat faces;
            detector->detect(bgr, faces);
            for (int i = 0; i < faces.rows; ++i) {
                FaceBox box;
                const float x = faces.at<float>(i, 0);
                const float y = faces.at<float>(i, 1);
                const float w = faces.at<float>(i, 2);
                const float h = faces.at<float>(i, 3);
                box.rect = QRectF(qreal(x), qreal(y), qreal(w), qreal(h));
                box.score = faces.at<float>(i, 14);
                box.landmarks.reserve(5);
                for (int k = 0; k < 5; ++k) {
                    const float lx = faces.at<float>(i, 4 + k * 2);
                    const float ly = faces.at<float>(i, 5 + k * 2);
                    box.landmarks.append(QPointF(qreal(lx), qreal(ly)));
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
