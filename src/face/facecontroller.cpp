// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "face/facecontroller.h"

#include "imageitem.h"
#include "imageview.h"

#include <QPen>
#include <QtConcurrent>

namespace biltoo::face {

FaceController::FaceController(QObject *parent)
    : QObject(parent)
    , m_detector(FaceDetector::createDefaultDetector())
{
}

FaceDetectorInfo FaceController::detectorInfo() const
{
    return m_detector ? m_detector->info() : FaceDetector::createNullDetector()->info();
}

void FaceController::setDetector(std::unique_ptr<FaceDetector> detector)
{
    m_detector = std::move(detector);
    if (!m_detector) {
        m_detector = FaceDetector::createNullDetector();
    }
}

void FaceController::setScoreThreshold(float t)
{
    m_scoreThreshold = qBound(0.05f, t, 0.99f);
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

void FaceController::detectAsync(const QString &path, const QImage &image)
{
    if (m_busy || !m_detector) {
        return;
    }
    if (image.isNull()) {
        FaceDetectionResult r;
        r.path = path;
        r.backendId = m_detector->info().id;
        r.error = QStringLiteral("No image pixels available for face detection.");
        m_last = r;
        emit detectionFinished();
        return;
    }

    setBusy(true);
    const int gen = ++m_generation;
    const float thr = m_scoreThreshold;
    // Copy for worker (QImage is implicitly shared; detach via copy).
    const QImage sample = image.copy();
    FaceDetector *detector = m_detector.get();

    (void)QtConcurrent::run([this, gen, path, sample, thr, detector]() {
        FaceDetectionResult r = detector->detect(sample, thr);
        r.path = path;
        QMetaObject::invokeMethod(
            this,
            [this, gen, r]() {
                if (gen != m_generation) {
                    return;
                }
                m_last = r;
                setBusy(false);
                emit detectionFinished();
            },
            Qt::QueuedConnection);
    });
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
        if (it && it->path() == m_last.path) {
            item = it;
            break;
        }
    }
    if (!item) {
        // Fall back to primary / first live item when path matches classic path.
        const auto items = view->liveItems();
        if (items.size() == 1) {
            item = items.first();
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
    boxPen.setWidthF(0);
    boxPen.setCosmetic(true);
    boxPen.setWidth(2);
    painter->setPen(boxPen);
    painter->setBrush(Qt::NoBrush);

    for (const FaceBox &face : m_last.faces) {
        const QRectF r(content.left() + face.rect.x() * sx,
                       content.top() + face.rect.y() * sy,
                       face.rect.width() * sx,
                       face.rect.height() * sy);
        painter->drawRect(r);

        if (m_showLandmarks) {
            QPen lmPen(QColor(255, 80, 80, 230));
            lmPen.setWidth(2);
            lmPen.setCosmetic(true);
            painter->setPen(lmPen);
            for (const QPointF &lp : face.landmarks) {
                const QPointF p(content.left() + lp.x() * sx,
                                content.top() + lp.y() * sy);
                painter->drawEllipse(p, 2.5, 2.5);
            }
            painter->setPen(boxPen);
        }
    }
    painter->restore();
}

} // namespace biltoo::face
