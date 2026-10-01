// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BILTOO_FACE_FACECONTROLLER_H
#define BILTOO_FACE_FACECONTROLLER_H

#include "face/facetypes.h"
#include "face/facedetector.h"

#include <QObject>
#include <QPainter>
#include <memory>

class ImageView;

namespace biltoo::face {

/**
 * Session-facing face detection state: owns the detector backend, last result,
 * overlay flags. UI (FacePanel) and ImageView chrome talk only to this type —
 * not to OpenCV.
 */
class FaceController : public QObject {
    Q_OBJECT
public:
    explicit FaceController(QObject *parent = nullptr);

    FaceDetectorInfo detectorInfo() const;

    /** Replace backend (tests / future recogniser hooks). Takes ownership. */
    void setDetector(std::unique_ptr<FaceDetector> detector);

    float scoreThreshold() const { return m_scoreThreshold; }
    void setScoreThreshold(float t);

    bool overlayVisible() const { return m_overlayVisible; }
    void setOverlayVisible(bool on);

    bool showLandmarks() const { return m_showLandmarks; }
    void setShowLandmarks(bool on);

    bool isBusy() const { return m_busy; }
    const FaceDetectionResult &lastResult() const { return m_last; }

    /** Clear last result and request a viewport update. */
    void clearResults();

    /**
     * Run detection on @p image for @p path (async). Emits detectionFinished
     * when done. Concurrent runs are ignored while busy.
     */
    void detectAsync(const QString &path, const QImage &image);

    /** Scene-space overlay for the current ImageView (boxes on matching path). */
    void paintSceneOverlay(QPainter *painter, ImageView *view) const;

signals:
    /** Fired after detectAsync completes (or clearResults). Read lastResult(). */
    void detectionFinished();
    void busyChanged(bool busy);
    void overlaySettingsChanged();

private:
    void setBusy(bool on);

    std::unique_ptr<FaceDetector> m_detector;
    FaceDetectionResult m_last;
    float m_scoreThreshold = 0.6f;
    bool m_overlayVisible = true;
    bool m_showLandmarks = true;
    bool m_busy = false;
    int m_generation = 0;
};

} // namespace biltoo::face

#endif
