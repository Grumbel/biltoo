// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/faceembedder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

#include <cmath>
#include <mutex>

#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#endif

namespace biltoo::face {
namespace {

QStringList sfaceSearchPaths()
{
    QStringList paths;
    if (const QByteArray env = qgetenv("BILTOO_FACE_SFACE_MODEL"); !env.isEmpty()) {
        paths.append(QString::fromLocal8Bit(env));
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    paths.append(appDir + QStringLiteral("/../share/biltoo/models/face_recognition_sface_2021dec.onnx"));
    paths.append(appDir + QStringLiteral("/models/face_recognition_sface_2021dec.onnx"));
    const QStringList dataRoots = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &root : dataRoots) {
        paths.append(root + QStringLiteral("/models/face_recognition_sface_2021dec.onnx"));
    }
    paths.append(QStringLiteral("data/models/face_recognition_sface_2021dec.onnx"));
    paths.append(QStringLiteral("/usr/share/biltoo/models/face_recognition_sface_2021dec.onnx"));
    return paths;
}

QString resolveSfaceModelPath()
{
    for (const QString &p : sfaceSearchPaths()) {
        if (QFileInfo::exists(p) && QFileInfo(p).isFile()) {
            return QFileInfo(p).absoluteFilePath();
        }
    }
    return {};
}

#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV

cv::Mat qImageToBgr(const QImage &image)
{
    QImage rgb = image.convertToFormat(QImage::Format_RGB888).copy();
    if (rgb.isNull()) {
        return {};
    }
    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3,
                   const_cast<uchar *>(rgb.constBits()),
                   static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
    if (!bgr.isContinuous()) {
        bgr = bgr.clone();
    }
    return bgr;
}

/** YuNet / FaceDetectorYN 1×15 row expected by FaceRecognizerSF::alignCrop. */
cv::Mat faceRowFromBox(const FaceBox &face)
{
    cv::Mat row(1, 15, CV_32F, cv::Scalar(0));
    row.at<float>(0, 0) = float(face.rect.x());
    row.at<float>(0, 1) = float(face.rect.y());
    row.at<float>(0, 2) = float(face.rect.width());
    row.at<float>(0, 3) = float(face.rect.height());
    for (int k = 0; k < 5; ++k) {
        if (k < face.landmarks.size()) {
            row.at<float>(0, 4 + k * 2) = float(face.landmarks[k].x());
            row.at<float>(0, 5 + k * 2) = float(face.landmarks[k].y());
        } else {
            // Synthetic landmarks from box (worse alignment, still usable).
            const float cx = float(face.rect.center().x());
            const float cy = float(face.rect.center().y());
            const float w = float(face.rect.width());
            const float h = float(face.rect.height());
            const float pts[5][2] = {
                {cx - 0.2f * w, cy - 0.15f * h},
                {cx + 0.2f * w, cy - 0.15f * h},
                {cx, cy},
                {cx - 0.15f * w, cy + 0.25f * h},
                {cx + 0.15f * w, cy + 0.25f * h},
            };
            row.at<float>(0, 4 + k * 2) = pts[k][0];
            row.at<float>(0, 5 + k * 2) = pts[k][1];
        }
    }
    row.at<float>(0, 14) = face.score > 0.f ? face.score : 1.f;
    return row;
}

class SfaceFaceEmbedder final : public FaceEmbedder {
public:
    explicit SfaceFaceEmbedder(QString modelPath)
        : m_modelPath(std::move(modelPath))
    {
    }

    FaceBackendInfo info() const override
    {
        FaceBackendInfo i;
        i.id = QStringLiteral("sface");
        i.displayName = QStringLiteral("OpenCV SFace");
        i.available = !m_modelPath.isEmpty();
        i.detail = m_modelPath.isEmpty()
            ? QStringLiteral("SFace ONNX model not found (set BILTOO_FACE_SFACE_MODEL).")
            : m_modelPath;
        return i;
    }

    int embeddingDim() const override { return 128; }

    QVector<float> embed(const QImage &image, const FaceBox &face) const override
    {
        if (image.isNull() || face.rect.width() < 2 || face.rect.height() < 2
            || m_modelPath.isEmpty()) {
            return {};
        }
        cv::Mat bgr = qImageToBgr(image);
        if (bgr.empty()) {
            return {};
        }
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_sf) {
                m_sf = cv::FaceRecognizerSF::create(m_modelPath.toStdString(), std::string());
            }
            if (!m_sf) {
                return {};
            }
            cv::Mat faceRow = faceRowFromBox(face);
            cv::Mat aligned;
            m_sf->alignCrop(bgr, faceRow, aligned);
            if (aligned.empty()) {
                return {};
            }
            cv::Mat feature;
            m_sf->feature(aligned, feature);
            if (feature.empty()) {
                return {};
            }
            feature = feature.reshape(1, 1);
            QVector<float> out;
            out.reserve(feature.cols);
            for (int i = 0; i < feature.cols; ++i) {
                out.append(feature.at<float>(0, i));
            }
            return out;
        } catch (...) {
            return {};
        }
    }

    float matchCosine(const QVector<float> &a, const QVector<float> &b) const override
    {
        if (a.isEmpty() || a.size() != b.size()) {
            return 0.f;
        }
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_sf) {
                m_sf = cv::FaceRecognizerSF::create(m_modelPath.toStdString(), std::string());
            }
            if (!m_sf) {
                return 0.f;
            }
            cv::Mat ma(1, a.size(), CV_32F);
            cv::Mat mb(1, b.size(), CV_32F);
            for (int i = 0; i < a.size(); ++i) {
                ma.at<float>(0, i) = a[i];
                mb.at<float>(0, i) = b[i];
            }
            return m_sf->match(ma, mb, cv::FaceRecognizerSF::FR_COSINE);
        } catch (...) {
            return 0.f;
        }
    }

private:
    QString m_modelPath;
    mutable std::mutex m_mutex;
    mutable cv::Ptr<cv::FaceRecognizerSF> m_sf;
};

#endif // BILTOO_HAVE_OPENCV

} // namespace

std::unique_ptr<FaceEmbedder> FaceEmbedder::createDefaultEmbedder()
{
#if defined(BILTOO_HAVE_OPENCV) && BILTOO_HAVE_OPENCV
    const QString model = resolveSfaceModelPath();
    if (!model.isEmpty()) {
        return std::make_unique<SfaceFaceEmbedder>(model);
    }
#endif
    return createNullEmbedder();
}

} // namespace biltoo::face
