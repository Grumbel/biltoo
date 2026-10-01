// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/facecontroller.h"

#include "imageitem.h"
#include "imageview.h"

#include <QFont>
#include <QFontMetrics>
#include <QPen>
#include <QtConcurrent>
#include <QtMath>

namespace biltoo::face {

FaceController::FaceController(QObject *parent)
    : QObject(parent)
    , m_detector(FaceDetector::createDefaultDetector())
    , m_embedder(FaceEmbedder::createDefaultEmbedder())
{
    m_gallery.load();
}

FaceBackendInfo FaceController::detectorInfo() const
{
    return m_detector ? m_detector->info() : FaceDetector::createNullDetector()->info();
}

FaceBackendInfo FaceController::embedderInfo() const
{
    return m_embedder ? m_embedder->info() : FaceEmbedder::createNullEmbedder()->info();
}

void FaceController::setDetector(std::unique_ptr<FaceDetector> detector)
{
    m_detector = std::move(detector);
    if (!m_detector) {
        m_detector = FaceDetector::createNullDetector();
    }
}

void FaceController::setEmbedder(std::unique_ptr<FaceEmbedder> embedder)
{
    m_embedder = std::move(embedder);
    if (!m_embedder) {
        m_embedder = FaceEmbedder::createNullEmbedder();
    }
}

void FaceController::setScoreThreshold(float t)
{
    m_scoreThreshold = qBound(0.05f, t, 0.99f);
}

void FaceController::setMatchThreshold(float t)
{
    m_matchThreshold = qBound(0.05f, t, 0.99f);
}

void FaceController::setOverlayVisible(bool on)
{
    if (m_overlayVisible == on) {
        return;
    }
    m_overlayVisible = on;
    emit overlaySettingsChanged();
}

void FaceController::setShowLandmarks(bool on)
{
    if (m_showLandmarks == on) {
        return;
    }
    m_showLandmarks = on;
    emit overlaySettingsChanged();
}

void FaceController::clearResults()
{
    ++m_generation;
    m_last = FaceDetectionResult{};
    m_lastImage = QImage();
    setBusy(false);
    emit detectionFinished();
}

void FaceController::setBusy(bool on)
{
    if (m_busy == on) {
        return;
    }
    m_busy = on;
    emit busyChanged(m_busy);
}

void FaceController::recognizeInPlace(FaceDetectionResult &result, const QImage &image) const
{
    if (!m_embedder || !m_embedder->info().available || image.isNull()) {
        return;
    }
    result.embedBackendId = m_embedder->info().id;
    const QString modelId = m_embedder->info().id;
    for (FaceBox &face : result.faces) {
        face.embedding = m_embedder->embed(image, face);
        face.embeddingModelId = modelId;
        if (face.embedding.isEmpty()) {
            continue;
        }
        float score = 0.f;
        const int idx = m_gallery.bestMatch(face.embedding, modelId, &score);
        if (idx >= 0 && score >= m_matchThreshold) {
            face.matchScore = score;
            face.matchId = m_gallery.identities().at(idx).id;
            face.matchLabel = m_gallery.identities().at(idx).label;
        } else {
            face.matchScore = score;
            face.matchId.clear();
            face.matchLabel.clear();
        }
    }
}

void FaceController::detectAsync(const QString &path, const QImage &image,
                                 SessionImageId sessionId)
{
    if (!m_detector) {
        return;
    }
    if (image.isNull()) {
        ++m_generation;
        FaceDetectionResult r;
        r.path = path;
        r.sessionId = sessionId;
        r.backendId = m_detector->info().id;
        r.error = QStringLiteral("No image pixels available for face detection.");
        m_last = r;
        m_lastImage = QImage();
        setBusy(false);
        emit detectionFinished();
        return;
    }

    const int gen = ++m_generation;
    setBusy(true);
    const float thr = m_scoreThreshold;
    const QImage sample = image.copy();
    FaceDetector *detector = m_detector.get();

    (void)QtConcurrent::run([this, gen, path, sessionId, sample, thr, detector]() {
        FaceDetectionResult r = detector->detect(sample, thr);
        r.path = path;
        r.sessionId = sessionId;
        recognizeInPlace(r, sample);
        QMetaObject::invokeMethod(
            this,
            [this, gen, r, sample]() {
                if (gen != m_generation) {
                    return;
                }
                m_last = r;
                m_lastImage = sample;
                setBusy(false);
                emit detectionFinished();
            },
            Qt::QueuedConnection);
    });
}

bool FaceController::enrollFace(int faceIndex, const QString &label)
{
    if (label.trimmed().isEmpty() || faceIndex < 0 || faceIndex >= m_last.faces.size()) {
        return false;
    }
    FaceBox &face = m_last.faces[faceIndex];
    if (face.embedding.isEmpty() && m_embedder && m_embedder->info().available
        && !m_lastImage.isNull()) {
        face.embedding = m_embedder->embed(m_lastImage, face);
        face.embeddingModelId = m_embedder->info().id;
    }
    if (face.embedding.isEmpty()) {
        return false;
    }
    FaceIdentity id;
    id.label = label.trimmed();
    id.embedding = face.embedding;
    id.embeddingModelId = face.embeddingModelId;
    const QString stored = m_gallery.upsert(id);
    face.matchId = stored;
    face.matchLabel = id.label;
    face.matchScore = 1.f;
    emit galleryChanged();
    emit detectionFinished();
    return true;
}

void FaceController::paintSceneOverlay(QPainter *painter, ImageView *view) const
{
    if (!painter || !view || !m_overlayVisible || m_last.faces.isEmpty()) {
        return;
    }
    if (m_last.imageSize.isEmpty()) {
        return;
    }

    ImageItem *item = nullptr;
    for (ImageItem *it : view->liveItems()) {
        if (!it) {
            continue;
        }
        if (m_last.sessionId != kInvalidSessionImageId
            && it->sessionId() == m_last.sessionId) {
            item = it;
            break;
        }
        if (!m_last.path.isEmpty() && it->path() == m_last.path) {
            item = it;
            break;
        }
    }
    if (!item) {
        return;
    }

    const QRectF content = item->contentRect();
    if (content.isEmpty()) {
        return;
    }
    const qreal sx = content.width() / qreal(m_last.imageSize.width());
    const qreal sy = content.height() / qreal(m_last.imageSize.height());

    painter->save();
    painter->setTransform(item->sceneTransform(), true);

    QPen boxPen(QColor(0, 220, 120, 220));
    boxPen.setCosmetic(true);
    boxPen.setWidth(2);
    painter->setPen(boxPen);
    painter->setBrush(Qt::NoBrush);

    QFont font = painter->font();
    font.setPointSizeF(10);
    font.setBold(true);
    painter->setFont(font);

    for (const FaceBox &face : m_last.faces) {
        const QRectF r(content.left() + face.rect.x() * sx,
                       content.top() + face.rect.y() * sy,
                       face.rect.width() * sx,
                       face.rect.height() * sy);
        painter->drawRect(r);

        if (!face.matchLabel.isEmpty()) {
            const QString tag =
                QStringLiteral("%1 (%2)").arg(face.matchLabel).arg(face.matchScore, 0, 'f', 2);
            const QFontMetrics fm(painter->font());
            const QRect tr = fm.boundingRect(tag).adjusted(-3, -2, 3, 2);
            QRectF bg(r.left(), r.top() - tr.height() - 2, tr.width(), tr.height());
            if (bg.top() < content.top()) {
                bg.moveTop(r.bottom() + 2);
            }
            painter->fillRect(bg, QColor(0, 0, 0, 160));
            painter->setPen(QColor(255, 255, 255));
            painter->drawText(bg, Qt::AlignCenter, tag);
            painter->setPen(boxPen);
        }

        if (m_showLandmarks && !face.landmarks.isEmpty()) {
            const qreal faceMin = qMin(r.width(), r.height());
            const qreal rad = qBound(4.0, faceMin * 0.035, 28.0);
            QPen lmPen(QColor(255, 60, 60, 240));
            lmPen.setCosmetic(true);
            lmPen.setWidth(2);
            painter->setPen(lmPen);
            painter->setBrush(QColor(255, 80, 80, 180));
            for (const QPointF &lp : face.landmarks) {
                const QPointF p(content.left() + lp.x() * sx,
                                content.top() + lp.y() * sy);
                painter->drawEllipse(p, rad, rad);
            }
            painter->setBrush(Qt::NoBrush);
            painter->setPen(boxPen);
        }
    }
    painter->restore();
}

} // namespace biltoo::face
