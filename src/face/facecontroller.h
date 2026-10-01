// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BILTOO_FACE_FACECONTROLLER_H
#define BILTOO_FACE_FACECONTROLLER_H

#include "face/facetypes.h"
#include "face/facedetector.h"
#include "face/faceembedder.h"
#include "face/facegallery.h"

#include <QImage>
#include <QObject>
#include <QPainter>
#include <memory>

class ImageView;

namespace biltoo::face {

/**
 * Session-facing face detection + recognition: detector, embedder, gallery.
 * UI talks only to this type — not to OpenCV.
 */
class FaceController : public QObject {
    Q_OBJECT
public:
    explicit FaceController(QObject *parent = nullptr);

    FaceBackendInfo detectorInfo() const;
    FaceBackendInfo embedderInfo() const;

    void setDetector(std::unique_ptr<FaceDetector> detector);
    void setEmbedder(std::unique_ptr<FaceEmbedder> embedder);

    float scoreThreshold() const { return m_scoreThreshold; }
    void setScoreThreshold(float t);

    /** Cosine threshold for accepting a gallery match (SFace ~0.36 default). */
    float matchThreshold() const { return m_matchThreshold; }
    void setMatchThreshold(float t);

    bool overlayVisible() const { return m_overlayVisible; }
    void setOverlayVisible(bool on);

    bool showLandmarks() const { return m_showLandmarks; }
    void setShowLandmarks(bool on);

    bool isBusy() const { return m_busy; }
    const FaceDetectionResult &lastResult() const { return m_last; }
    const FaceGallery &gallery() const { return m_gallery; }

    void clearResults();

    /**
     * Detect, then embed + match against the gallery (async).
     * Keeps the analysed QImage for later enroll of face index.
     */
    void detectAsync(const QString &path, const QImage &image,
                     SessionImageId sessionId = kInvalidSessionImageId);

    /**
     * Enroll face @p faceIndex from the last detection under @p label.
     * Requires a successful prior detect with embedding.
     */
    bool enrollFace(int faceIndex, const QString &label);

    void paintSceneOverlay(QPainter *painter, ImageView *view) const;

signals:
    void detectionFinished();
    void busyChanged(bool busy);
    void overlaySettingsChanged();
    void galleryChanged();

private:
    void setBusy(bool on);
    void recognizeInPlace(FaceDetectionResult &result, const QImage &image) const;

    std::unique_ptr<FaceDetector> m_detector;
    std::unique_ptr<FaceEmbedder> m_embedder;
    FaceGallery m_gallery;
    FaceDetectionResult m_last;
    /** Sample used for last detection (for enroll / re-embed). */
    QImage m_lastImage;
    float m_scoreThreshold = 0.6f;
    float m_matchThreshold = 0.363f; ///< OpenCV SFace cosine default band.
    bool m_overlayVisible = true;
    bool m_showLandmarks = true;
    bool m_busy = false;
    int m_generation = 0;
};

} // namespace biltoo::face

#endif
