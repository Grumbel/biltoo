// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Multi-mode viewport pan (owned by ViewShellChrome).

#include "view/viewshellchrome.h"
#include "imageview.h"
#include "view/viewtransform.h"
#include "view/canvaspatterngeometry.h"
#include "shell/centreprogress.h"
#include "crop/cropcontroller.h"
#include "gallery/gallerycontroller.h"
#include "slideshow/slideshowcontroller.h"
#include "hud/hudgeometry.h"
#include <QRect>
#include <QColor>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QFont>
#include <QPainter>
#include <cmath>
#include <QPixmap>
#include <QtMath>
#include <cstdlib>
#include <cstdio>
#include <QStringList>
#include <QUrl>
#include <QWidget>
#include <QCursor>
#include <QMimeData>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QDragEnterEvent>
#include <QPoint>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include "imageitem.h"
#include "image/toolpolicy.h"
#include "display/displaypipelinecontroller.h"

#include <QMouseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QScrollBar>

bool ViewShellChrome::tryMousePressPan(QMouseEvent *event)
{
    if (!m_view || !event) {
        return false;
    }
    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (!m_view->hostSlideshow().dwell().isMotionActive()
        && (event->button() == Qt::MiddleButton
            || (event->button() == Qt::LeftButton
                && ((m_view->isImageMode() && m_viewport.isImageModeLeftDragPan())
                    || (m_view->isWorkspaceMode()
                        && m_view->currentTool() == Tool::Pan)
                    || (m_view->isGalleryMode()
                        && (event->modifiers() & Qt::AltModifier))
                    || (event->modifiers() & Qt::AltModifier))))) {
        if (!(m_view->isWorkspaceMode() && (event->modifiers() & Qt::ShiftModifier)
              && event->button() == Qt::LeftButton)) {
            m_viewport.beginPan(event->pos());
            m_view->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return true;
        }
    }
    if (event->button() == Qt::MiddleButton
        && !m_view->hostSlideshow().dwell().isMotionActive()) {
        m_viewport.beginPan(event->pos());
        m_view->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return true;
    }
    return false;
}

bool ViewShellChrome::tryMouseMovePan(QMouseEvent *event)
{
    if (!m_view || !event || !m_viewport.isPanning()) {
        return false;
    }
    // Ken Burns / pure-clock motion owns the viewport — do not fight it.
    if (m_view->hostSlideshow().dwell().isMotionActive()) {
        m_viewport.endPan();
        event->accept();
        return true;
    }
    const QPoint delta = m_viewport.panDeltaFrom(event->pos());
    m_viewport.updatePanPos(event->pos());
    // Grow the free-form sceneRect with the view so middle-drag is never
    // clamped against a stale zero-range scrollbar.
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    if (QScrollBar *h = m_view->horizontalScrollBar()) {
        h->setValue(h->value() - delta.x());
    }
    if (QScrollBar *v = m_view->verticalScrollBar()) {
        v->setValue(v->value() - delta.y());
    }
    // Tile LOD timer stops once the viewport is covered. Panning changes the
    // visible set without a zoom/climb event — coalesce issues (not per move).
    m_view->hostDisplayPipeline().scheduleTileLodAfterInteraction(32);
    event->accept();
    return true;
}

bool ViewShellChrome::tryMouseReleasePan(QMouseEvent *event)
{
    if (!m_view || !event || !m_viewport.isPanning()
        || (event->button() != Qt::MiddleButton && event->button() != Qt::LeftButton)) {
        return false;
    }
    m_viewport.endPan();
    m_view->restoreToolCursor();
    m_view->hostDisplayPipeline().tickPrimaryTileLod(8);
    event->accept();
    return true;
}

void ViewShellChrome::updateMouseInfo(const QPoint &viewPos)
{
    if (!m_view) {
        return;
    }
    ImageMouseInfo info;
    const QPointF scenePos = m_view->mapToScene(viewPos);

    // Prefer the topmost item under the cursor
    ImageItem *hit = nullptr;
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        for (QGraphicsItem *gi : scene->items(scenePos)) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = item;
                break;
            }
        }
    }

    if (hit) {
        const QPoint pixel = hit->pixelAtScenePos(scenePos);
        if (pixel.x() >= 0) {
            info.valid = true;
            info.imagePos = pixel;
            info.pixelColor = hit->colorAtPixel(pixel);
            info.path = hit->path();
        }
    }

    if (m_viewport.setMouseInfo(info)) {
        emit m_view->mouseInfoChanged(m_viewport.currentMouseInfo());
    }
}

void ViewShellChrome::onLeave()
{
    if (!m_view) {
        return;
    }
    if (m_viewport.hasMouseInfo()) {
        m_viewport.clearMouseInfo();
        emit m_view->mouseInfoChanged(m_viewport.currentMouseInfo());
    }
}

namespace {

bool mimeAcceptsPaths(const QMimeData *mime)
{
    return mime
        && (mime->hasUrls()
            || mime->hasFormat(QStringLiteral("application/x-biltoo-paths")));
}

} // namespace

void ViewShellChrome::dragEnterEvent(QDragEnterEvent *event)
{
    if (mimeAcceptsPaths(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ViewShellChrome::dragMoveEvent(QDragMoveEvent *event)
{
    if (mimeAcceptsPaths(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ViewShellChrome::dropEvent(QDropEvent *event)
{
    if (!m_view || !event->mimeData()) {
        if (event) {
            event->ignore();
        }
        return;
    }
    const QByteArray pathBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-paths"));
    const bool hasInternal = !pathBytes.isEmpty();
    if (!event->mimeData()->hasUrls() && !hasInternal) {
        event->ignore();
        return;
    }
    // Prefer global→viewport→scene. Drop events may land on the view or the
    // OpenGL viewport child; widget-local position() is then wrong for mapToScene.
    // QDropEvent has no portable globalPosition() here — use the cursor.
    QWidget *vp = m_view->viewport();
    const QPoint viewPos = vp ? vp->mapFromGlobal(QCursor::pos()) : event->position().toPoint();
    const QPointF scenePos = m_view->mapToScene(viewPos);
    QList<qint64> sessionIds;
    const QByteArray idBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-session-ids"));
    if (!idBytes.isEmpty()) {
        for (const QByteArray &tok : idBytes.split(',')) {
            bool ok = false;
            const qint64 v = tok.trimmed().toLongLong(&ok);
            sessionIds.append(ok ? v : 0);
        }
    }
    QStringList internalPaths;
    if (hasInternal) {
        internalPaths = QString::fromUtf8(pathBytes).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        std::fprintf(stderr,
                     "biltoo/drop: ImageView::dropEvent viewPos=(%d,%d) scene=(%.1f,%.1f) "
                     "mode=W%d G%d\n",
                     viewPos.x(), viewPos.y(), scenePos.x(), scenePos.y(),
                     m_view->isWorkspaceMode() ? 1 : 0, m_view->isGalleryMode() ? 1 : 0);
    }
    emit m_view->filesDropped(event->mimeData()->urls(), event->modifiers(), scenePos,
                              /*hasScenePos=*/true, sessionIds, internalPaths);
    event->acceptProposedAction();
}

void ViewShellChrome::paintEmptySessionInvite(QPainter &painter) const
{
    if (!m_view) {
        return;
    }
    // Empty session: invite the user to open or drop images.
    // Suppress while progress is active (expand / size resolve / tile load).
    if (!(m_view->liveItems().isEmpty() && !m_view->hostImage().hasClassicPath()
          && !m_view->hostCrop().session().active()
          && m_view->hostCentreProgress().titleRef().isEmpty()
          && !m_view->hostGallerySizeResolve().active())) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont titleFont = m_view->font();
    titleFont.setPointSize(HudGeometry::clampTitlePointSize(titleFont.pointSize()));
    titleFont.setBold(true);
    QFont hintFont = m_view->font();
    hintFont.setPointSize(HudGeometry::clampHintPointSize(hintFont.pointSize()));
    const QString title = m_view->isWorkspaceMode()
        ? QCoreApplication::translate("ImageView", "Drop images here")
        : QCoreApplication::translate("ImageView", "Drop images here or open a file");
    const QString hint = m_view->isWorkspaceMode()
        ? QCoreApplication::translate(
              "ImageView",
              "Drag files or filmstrip thumbnails onto the canvas to place images")
        : QCoreApplication::translate(
              "ImageView", "File → Open…  ·  Ctrl+O  ·  drag and drop");
    const QFontMetrics titleFm(titleFont);
    const QFontMetrics hintFm(hintFont);
    const int gap = 8;
    const int totalH = titleFm.height() + gap + hintFm.height();
    QWidget *vp = m_view->viewport();
    const int vw = vp ? vp->width() : 0;
    const int vh = vp ? vp->height() : 0;
    const int cy = vh / 2 - totalH / 2;
    painter.setFont(titleFont);
    painter.setPen(QColor(220, 220, 220, 230));
    painter.drawText(QRect(0, cy, vw, titleFm.height()),
                     Qt::AlignHCenter | Qt::AlignVCenter, title);
    painter.setFont(hintFont);
    painter.setPen(QColor(180, 180, 180, 200));
    painter.drawText(QRect(0, cy + titleFm.height() + gap, vw, hintFm.height()),
                     Qt::AlignHCenter | Qt::AlignVCenter, hint);

    // Edge-zone captions (Image mode uses these corners once a session is open).
    if (m_view->isImageMode() || (!m_view->isWorkspaceMode() && !m_view->isGalleryMode())) {
        QFont edgeFont = m_view->font();
        edgeFont.setPointSize(HudGeometry::clampEdgePointSize(edgeFont.pointSize()));
        painter.setFont(edgeFont);
        painter.setPen(QColor(160, 160, 160, 180));
        const QFontMetrics efm(edgeFont);
        const int em = 16;
        const QString prevLabel = QCoreApplication::translate("ImageView", "← Previous");
        const QString nextLabel = QCoreApplication::translate("ImageView", "Next →");
        const QString backLabel =
            QCoreApplication::translate("ImageView", "↑ Back to Gallery / Workspace");
        painter.drawText(QRect(em, vh / 2 - efm.height() / 2,
                               efm.horizontalAdvance(prevLabel) + 8, efm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, prevLabel);
        const int nextW = efm.horizontalAdvance(nextLabel) + 8;
        painter.drawText(QRect(vw - em - nextW, vh / 2 - efm.height() / 2, nextW, efm.height()),
                         Qt::AlignRight | Qt::AlignVCenter, nextLabel);
        const int backW = efm.horizontalAdvance(backLabel) + 8;
        painter.drawText(QRect((vw - backW) / 2, em, backW, efm.height()),
                         Qt::AlignHCenter | Qt::AlignTop, backLabel);
    }
    painter.restore();
}

void ViewShellChrome::paintHudPanels(QPainter &painter) const
{
    if (!m_view) {
        return;
    }
    // HUD layout:
    //   top-left  — transient actions (slideshow, fit, …), never Next/Prev
    //   top-right — session index [i/n]
    //   bottom    — filename (+ technical detail when the HUD is pinned)
    // Crop mode: always show a pinned “Crop mode” cue so the tool state is clear.
    const QString ssPrefetchLine = m_view->hostSlideshow().slideshowPrefetchHudLine();
    // Loading · … only in the extended (pinned) HUD — not as a free-floating
    // chip during slideshow or normal Image browsing.
    const QString loadingLine = m_view->hostHudPrefs().isVisible() ? m_view->loadingStatusHudLine() : QString();
    if (m_view->hostCrop().session().active() || m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isVisible() || m_view->hostHudFlash().isIdentityPulse()
        || m_view->hostSlideshow().hud().isPausedHud() || m_view->hostGallerySizeResolve().active()
        || !m_view->hostCentreProgress().titleRef().isEmpty()
        || !ssPrefetchLine.isEmpty()
        || !m_view->hostGallery().hoverPath().isEmpty()) {
        // Prefer the user preference (Preferences → HUD), not the widget font.
        QFont f = m_view->font();
        const int pt = m_view->hostHudPrefs().effectiveFontPointSize();
        f.setPointSize(pt);
        QFont boldF = f;
        boldF.setBold(true);
        const QFontMetrics fm(f);
        const QFontMetrics fmBold(boldF);
        const int margin = 10;
        const int pad = 8;
        const int lineGap = 2;
        const int viewW = m_view->viewport()->width();
        const int viewH = m_view->viewport()->height();

        struct HudLine {
            QString text;
            bool bold = false;
        };

        auto drawPanel = [&](const QList<HudLine> &lines, int anchorX, int anchorY,
                             bool fromRight, bool fromBottom, bool centre = false) {
            if (lines.isEmpty()) {
                return;
            }
            const int maxBgW = HudGeometry::maxPanelBgW(viewW, margin);
            const int maxTextW = HudGeometry::maxPanelTextW(maxBgW, pad);
            QList<HudLine> drawn;
            int textW = 0;
            int textH = 0;
            for (const HudLine &hl : lines) {
                const QFontMetrics &m = hl.bold ? fmBold : fm;
                for (const QString &w : HudGeometry::wrapHudLine(hl.text, m, maxTextW)) {
                    drawn.append({w, hl.bold});
                    HudGeometry::accumulateLineSize(&textW, &textH, m, w, maxTextW);
                }
            }
            if (drawn.isEmpty()) {
                return;
            }
            if (drawn.size() > 1) {
                textH += lineGap * (drawn.size() - 1);
            }
            const HudGeometry::PanelBox box = HudGeometry::placePanel(
                viewW, viewH, textW, textH, margin, pad, anchorX, anchorY,
                fromRight, fromBottom, centre);
            const QRect bg = HudGeometry::panelRect(box);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_view->hostHudPrefs().effectivePanelColor());
            painter.drawRoundedRect(bg, 6, 6);
            painter.setPen(m_view->hostHudPrefs().effectiveTextColor());
            int ty = bg.top() + pad;
            const int textAreaW = bg.width() - 2 * pad;
            for (const HudLine &hl : drawn) {
                const QFont &lf = hl.bold ? boldF : f;
                const QFontMetrics &m = hl.bold ? fmBold : fm;
                painter.setFont(lf);
                painter.drawText(QRect(bg.left() + pad, ty, textAreaW, m.height()),
                                 Qt::AlignLeft | Qt::AlignVCenter, hl.text);
                ty += m.height() + lineGap;
            }
        };

        // Top-left: crop mode cue (persistent while active), slideshow paused
        // cue (persistent until resume/stop), or transient flash.
        if (m_view->hostCrop().session().active()) {
            drawPanel({{QCoreApplication::translate("ImageView", "Crop mode"), true},
                       {QCoreApplication::translate("ImageView", "Handles · Reset · Apply · Esc"), false}},
                      margin, margin, false, false);
        } else if (m_view->hostSlideshow().hud().isPausedHud()) {
            drawPanel({{QCoreApplication::translate("ImageView", "❚❚  Paused"), true},
                       {QCoreApplication::translate("ImageView", "Space: resume · Esc: leave"), false}},
                      margin, margin, false, false);
        } else if (!m_view->hostCentreProgress().titleRef().isEmpty()) {
            QList<HudLine> lines;
            lines.append({m_view->hostCentreProgress().titleRef(), true});
            if (!m_view->hostCentreProgress().detailRef().isEmpty()) {
                lines.append({m_view->hostCentreProgress().detailRef(), false});
            }
            // Non-blocking background work → sticky top-left. Only true
            // blockers (archive expand / open progress) use the viewport centre.
            const bool corner =
                m_view->hostCentreProgress().matchesTitlePrefix(QCoreApplication::translate("ImageView", "Loading tiles"))
                || m_view->hostCentreProgress().matchesTitlePrefix(QCoreApplication::translate("ImageView", "Improving previews"))
                || m_view->hostCentreProgress().matchesTitlePrefix(QCoreApplication::translate("ImageView", "Resolving sizes"));
            if (corner) {
                drawPanel(lines, margin, margin, false, false, false);
            } else {
                drawPanel(lines, 0, 0, false, false, true);
            }
        } else if (m_view->hostGallerySizeResolve().active() && m_view->hostGallerySizeResolve().total() > 0) {
            // Fallback if title was cleared but gate still active (interactive).
            const int done = ViewTransform::nonNeg(
                qint64(m_view->hostGallerySizeResolve().total())
                - qint64(m_view->hostGallerySizeResolve().pendingCount()));
            drawPanel({{QCoreApplication::translate("ImageView", "Resolving sizes…"), true},
                       {QCoreApplication::translate("ImageView", "%1 / %2").arg(done).arg(m_view->hostGallerySizeResolve().total()), false}},
                      margin, margin, false, false, false);
        } else if (m_view->hostHudFlash().isVisible() && m_view->hostHudFlash().hasAction()) {
            QString actionLine = m_view->hostHudFlash().actionText();
            if (m_view->hostHudFlash().hasDetail()) {
                actionLine += QLatin1Char(' ') + m_view->hostHudFlash().detailText();
            }
            drawPanel({{actionLine, true}}, margin, margin, false, false);
        } else if (m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isIdentityPulse()) {
            QList<HudLine> topLeft;
            if (!loadingLine.isEmpty()) {
                topLeft.append({loadingLine, true});
            }
            if (!ssPrefetchLine.isEmpty()) {
                topLeft.append({ssPrefetchLine, false});
            }
            if (m_view->hostPerf().isEnabled()) {
                const auto &perf = m_view->hostPerf();
                topLeft.append({
                    QCoreApplication::translate("ImageView", "FPS %1 · paint %2 ms · decode-win %3 ms (max %4)")
                        .arg(perf.fpsValue(), 0, 'f', 1)
                        .arg(perf.lastPaintUsValue() / 1000.0, 0, 'f', 1)
                        .arg(perf.lastDecodeWindowUsValue() / 1000.0, 0, 'f', 1)
                        .arg(perf.maxDecodeWindowUsValue() / 1000.0, 0, 'f', 1),
                    false});
            }
            ImageItem *focus = m_view->targetItem();
            if (!focus) {
                focus = m_view->primaryItem();
            }
            if (focus) {
                const QString q = m_view->hostDisplayPipeline().pixelQualityLabel(focus);
                if (!q.isEmpty()) {
                    const int edge = focus->displayPixelLongEdge();
                    const QString line = edge > 0 && !focus->hasDecodedPixels()
                        ? QCoreApplication::translate("ImageView", "%1 · %2px").arg(q).arg(edge)
                        : q;
                    topLeft.append({line, false});
                }
            }
            if (!topLeft.isEmpty()) {
                drawPanel(topLeft, margin, margin, false, false);
            }
        }

        // Top-right: session index — pinned HUD or brief identity pulse after
        // user navigation. Not during pure action flashes (slideshow start, …)
        // and not on automatic slideshow advance (pulseIdentity=false).
        const QString badge = m_view->hostSlideshow().sessionBadgeText();
        if (!badge.isEmpty() && (m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isIdentityPulse())) {
            drawPanel({{badge, true}}, 0, margin, true, false);
        }

        // Bottom: filename — pinned HUD, identity pulse after user nav, or gallery hover
        if (m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isIdentityPulse() || !m_view->hostGallery().hoverPath().isEmpty()) {
            QList<HudLine> bottom;
            const QString name = m_view->hudFileName();
            if (!name.isEmpty()) {
                bottom.append({name, true});
            }
            if (m_view->hostHudPrefs().isVisible()) {
                const QString tech = m_view->statusText();
                if (!tech.isEmpty() && tech != name) {
                    bottom.append({tech, false});
                }
            }
            drawPanel(bottom, margin, 0, false, true);
        }
    }


}

void ViewShellChrome::paintViewportOverlays(QPainter &painter)
{
    if (!m_view) {
        return;
    }
    // Viewport-device-pixel overlays (handles, HUD, slideshow cover). Identity
    // transform is set by drawForeground so this works on raster and GL viewports.

    m_view->hostText().paintRubberBandOverlay(painter);

    // Workspace chrome in viewport device pixels (constant on-screen size).
    if (m_view->hostCrop().session().active()) {
        m_view->hostCrop().paintCropOverlay(painter);
    }
    if (m_view->hostAttention().session().active()) {
        m_view->hostAttention().paintAttentionOverlay(painter);
    }
    m_view->hostWorkspace().paintViewportChrome(painter);

    // Letterbox fills the viewport during slideshow; edges/HUD paint after it.
    m_view->hostSlideshow().paintLetterboxComposite(painter);
    paintEmptySessionInvite(painter);

    // Edge chevrons: ImageController owns zone policy + paint.
    m_view->hostImage().drawEdgeAffordances(painter);

    paintHudPanels(painter);
    m_view->hostSlideshow().paintSeekbar(painter);
}

void ViewShellChrome::paintForeground(QPainter *painter, const QRectF &rect)
{
    if (!m_view) {
        return;
    }
    // Scene-space chrome owned by mode/text controllers.
    m_view->hostWorkspace().paintPageGuideOutline(painter, rect);
    m_view->hostText().paintSceneOverlays(painter);

    // Viewport-space overlays on the same painter as the scene (required for
    // QOpenGLWidget: a second QPainter(viewport()) after paintEvent whites out).
    if (!painter) {
        return;
    }
    // Gallery selection frames: scene-space overlay so item ItemCoordinateCache
    // is not invalidated on select or scroll (was painted inside ImageItem::paint).
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().paintSelectionFrames(painter, rect);
    }
    // Bare Gallery: skip HUD/edges/slideshow overlay pass.
    if (m_view->isGalleryMode()
        && !m_view->hostHudPrefs().isVisible()
        && !m_view->hostHudFlash().isVisible()
        && !m_view->hostHudFlash().isIdentityPulse()
        && !m_view->hostSlideshow().hud().isPausedHud()
        && !m_view->hostGallerySizeResolve().active()
        && m_view->hostCentreProgress().titleRef().isEmpty()
        && m_view->hostHoverEdge() == ImageView::EdgeZone::None
        && !m_view->hostCrop().session().active()
        && !m_view->hostSlideshow().dwell().isMotionActive()) {
        return;
    }
    painter->save();
    painter->resetTransform();
    if (QWidget *vp = m_view->viewport()) {
        const qreal dpr = vp->devicePixelRatioF();
        if (!qFuzzyCompare(dpr, 1.0)) {
            painter->scale(dpr, dpr);
        }
    }
    paintViewportOverlays(*painter);
    painter->restore();
}

void ViewShellChrome::paintCanvasBackground(QPainter *painter, const QRectF &rect,
                                            qreal viewScale)
{
    if (!painter || !m_view) {
        return;
    }
    viewScale = ViewTransform::sanitizeViewScale(viewScale);

    auto fillChecker = [&](const QColor &a, const QColor &b) {
        const qreal cell = CanvasPatternGeometry::checkerCellScene(viewScale);
        const qreal x0 = std::floor(rect.left() / cell) * cell;
        const qreal y0 = std::floor(rect.top() / cell) * cell;
        const qreal x1 = std::ceil(rect.right() / cell) * cell;
        const qreal y1 = std::ceil(rect.bottom() / cell) * cell;
        for (qreal y = y0; y < y1; y += cell) {
            for (qreal x = x0; x < x1; x += cell) {
                const int ix = static_cast<int>(std::floor(x / cell));
                const int iy = static_cast<int>(std::floor(y / cell));
                const bool dark = ((ix + iy) & 1) != 0;
                painter->fillRect(QRectF(x, y, cell, cell), dark ? a : b);
            }
        }
    };

    auto fillImageTile = [&](QPixmap &tileCache, const QString &path,
                             auto storeTile) {
        if (tileCache.isNull() && !path.isEmpty()) {
            QPixmap px(path);
            if (!px.isNull()) {
                storeTile(px, path);
            }
        }
        if (tileCache.isNull()) {
            painter->fillRect(rect, m_canvasBg.primaryColor());
            return;
        }
        const QPixmap &tile = tileCache;
        qreal tw = qreal(ViewTransform::atLeast1(tile.width()));
        qreal th = qreal(ViewTransform::atLeast1(tile.height()));
        const qreal lod = CanvasPatternGeometry::tileLodFactor(tw, viewScale);
        const qreal cellW = tw * lod;
        const qreal cellH = th * lod;
        const qreal x0 = std::floor(rect.left() / cellW) * cellW;
        const qreal y0 = std::floor(rect.top() / cellH) * cellH;
        const qreal x1 = std::ceil(rect.right() / cellW) * cellW;
        const qreal y1 = std::ceil(rect.bottom() / cellH) * cellH;
        for (qreal y = y0; y < y1; y += cellH) {
            for (qreal x = x0; x < x1; x += cellW) {
                painter->drawPixmap(QRectF(x, y, cellW, cellH), tile,
                                    QRectF(0, 0, tw, th));
            }
        }
    };

    auto paintMaterial = [&](const WorkspaceBackground &wb, QPixmap &tileCache,
                             auto storeTile) {
        if (wb.mode == WorkspaceBackgroundMode::Solid) {
            painter->fillRect(rect, wb.color.isValid() ? wb.color : m_canvasBg.primaryColor());
        } else if (wb.mode == WorkspaceBackgroundMode::Checkerboard) {
            const QColor a = wb.color.isValid() ? wb.color : m_canvasBg.primaryColor();
            const QColor b = wb.colorAlt.isValid() ? wb.colorAlt : a.lighter(120);
            fillChecker(a, b);
        } else if (wb.mode == WorkspaceBackgroundMode::ImageTile) {
            fillImageTile(tileCache, wb.imagePath, storeTile);
        } else if (wb.mode == WorkspaceBackgroundMode::ContentBlur) {
            // Image mode: cover-scale blur under the sharp item (viewport space).
            // Gallery (no single subject): configured solid color from the dialog.
            QImage src;
            QString path;
            if (m_view->isImageMode()) {
                ImageItem *item = m_view->primaryItem();
                if (!item) {
                    const SessionImageId sid = m_view->hostSessionId().currentIdValue();
                    if (sid != kInvalidSessionImageId) {
                        item = m_view->findItemBySessionId(sid);
                    }
                }
                if (!item && m_view->hostImage().hasClassicPath()) {
                    item = m_view->findItemForPath(m_view->hostImage().classicPath());
                }
                if (item) {
                    src = item->displayImage();
                    if (src.isNull()) {
                        src = item->sourceImage();
                    }
                    path = item->path();
                }
                if (path.isEmpty() && m_view->hostImage().hasClassicPath()) {
                    path = m_view->hostImage().classicPath();
                }
            }
            if (!src.isNull() && m_view->viewport()) {
                painter->save();
                painter->resetTransform();
                const QRect vr = m_view->viewport()->rect();
                const qint64 key = path.isEmpty() ? qint64(0) : qint64(qHash(path));
                m_view->hostSlideshow().paintZoomBlurUnderlay(painter, src, vr, key);
                painter->restore();
            } else {
                const QColor fill = wb.color.isValid() ? wb.color : m_canvasBg.primaryColor();
                painter->fillRect(rect, fill);
            }
        } else {
            painter->fillRect(rect, m_canvasBg.primaryColor());
        }
    };

    const bool wsOverride = m_view->isWorkspaceMode()
        && !m_canvasBg.isWorkspaceAppDefault()
        && !m_canvasBg.isWorkspaceShowDefault();
    const bool viewOverride =
        (m_view->isGalleryMode() || m_view->isImageMode()) && !m_canvasBg.isViewAppDefault();

    if (wsOverride) {
        paintMaterial(m_canvasBg.workspaceRef(), m_canvasBg.workspaceTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_canvasBg.setWorkspaceTile(px, path);
                      });
    } else if (viewOverride) {
        paintMaterial(m_canvasBg.viewRef(), m_canvasBg.viewTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_canvasBg.setViewTile(px, path);
                      });
    } else {
        if (!m_canvasBg.useChecker(m_view->isWorkspaceMode())) {
            painter->fillRect(rect, m_canvasBg.primaryColor());
        } else {
            fillChecker(m_canvasBg.primaryColor(), m_canvasBg.checkerAlt());
        }
    }
}

void ViewShellChrome::paintBackground(QPainter *painter, const QRectF &rect, qreal viewScale)
{
    if (!m_view) {
        return;
    }
    paintCanvasBackground(painter, rect, viewScale);

    // Virtualized Gallery: packed cell frames under items.
    if (m_view->isGalleryMode() && painter) {
        m_view->hostGallery().paintVirtualPlaceholders(painter, rect);
    }

    // Page guide paper under images.
    m_view->hostWorkspace().paintPageGuidePaper(painter, rect);
}

void ViewShellChrome::refreshScrollBarGeometry()
{
    if (!m_view) {
        return;
    }
    // fitInView / sceneRect changes can leave AsNeeded bars with a stale range
    // until policy is toggled. Re-apply the current policies to force
    // QAbstractScrollArea to recompute visibility (public API only).
    const auto h = m_view->horizontalScrollBarPolicy();
    const auto v = m_view->verticalScrollBarPolicy();
    if (h == Qt::ScrollBarAsNeeded || v == Qt::ScrollBarAsNeeded) {
        m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_view->setHorizontalScrollBarPolicy(h);
        m_view->setVerticalScrollBarPolicy(v);
    }
}

void ViewShellChrome::applyModeViewportPolicy(int viewMode)
{
    if (!m_view) {
        return;
    }
    // Gallery: BoundingRect — FullViewportUpdate repaints every tile on each
    // scroll/zoom tick and is unusable with large soft bitmaps. Soft upgrades
    // must call item->update() (installDisplayPixels already does).
    // Image/Workspace: FullViewportUpdate for HUD/chrome.
    using VM = ImageView::ViewMode;
    if (static_cast<VM>(viewMode) == VM::Gallery) {
        m_view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    } else {
        m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

bool ViewShellChrome::handleMousePress(QMouseEvent *event)
{
    if (!m_view || !event) {
        return false;
    }
    return m_view->hostSlideshow().tryMousePressSlideshowSeek(event)
        || m_view->hostAttention().tryMousePressAttention(event)
        || m_view->hostCrop().tryMousePressCrop(event)
        || m_view->hostImage().tryMousePressZoomRegion(event)
        || m_view->hostWorkspace().tryMousePressWorkspaceChrome(event)
        || m_view->hostText().tryMousePressLink(event)
        || m_view->hostText().tryMousePressRubber(event)
        || m_view->hostImage().tryMousePressEdges(event)
        || tryMousePressPan(event)
        || m_view->hostWorkspace().tryMousePressWorkspaceRotate(event)
        || m_view->hostGallery().tryMousePressGalleryRight(event)
        || m_view->hostGallery().tryMousePressGalleryLeft(event)
        || m_view->hostWorkspace().tryMousePressSelect(event);
}

bool ViewShellChrome::handleMouseMove(QMouseEvent *event)
{
    if (!m_view || !event) {
        return false;
    }
    if (m_view->hostText().tryMouseMoveRubber(event)) {
        return true;
    }
    m_view->hostText().updateMouseMoveLinkHover(event);
    if (m_view->hostAttention().tryMouseMoveAttention(event)
        || m_view->hostCrop().tryMouseMoveCropDrag(event)
        || tryMouseMovePan(event)
        || m_view->hostCrop().tryMouseMoveCropHover(event)
        || m_view->hostImage().tryMouseMoveZoomRegion(event)
        || m_view->hostGallery().tryMouseMoveGalleryDrag(event)) {
        return true;
    }
    updateMouseInfo(event->pos());
    if (m_view->hostWorkspace().tryMouseMovePageGuide(event)
        || m_view->hostWorkspace().tryMouseMoveGroupAndHandleDrag(event)
        || m_view->hostWorkspace().tryMouseMoveWorkspaceRotate(event)) {
        return true;
    }

    if (m_view->isImageMode()) {
        m_view->hostImage().updateHoverEdge(event->pos());
    }

    m_viewport.setHoverViewPos(event->pos());
    m_view->hostSlideshow().updateMouseMoveSlideshowSeek(event);
    m_view->hostGallery().updateGalleryHoverAt(m_viewport.hoverViewPos());
    m_view->hostWorkspace().updateMouseMoveWorkspaceChromeHover(event);
    return false;
}

bool ViewShellChrome::handleMouseRelease(QMouseEvent *event)
{
    if (!m_view || !event) {
        return false;
    }
    if (m_view->hostSlideshow().tryMouseReleaseSlideshowSeek(event)
        || m_view->hostText().tryMouseReleaseRubber(event)
        || m_view->hostAttention().tryMouseReleaseAttention(event)
        || m_view->hostCrop().tryMouseReleaseCrop(event)
        || m_view->hostImage().tryMouseReleaseZoomRegion(event)
        || m_view->hostWorkspace().tryMouseReleasePageGuide(event)
        || m_view->hostWorkspace().tryMouseReleaseGroupDrag(event)
        || m_view->hostWorkspace().tryMouseReleaseHandleDrag(event)
        || m_view->hostWorkspace().tryMouseReleaseWorkspaceRotate(event)
        || tryMouseReleasePan(event)) {
        return true;
    }
    m_view->hostGallery().clearGalleryDragArm();
    m_view->hostWorkspace().tryMouseReleaseItemDrag(event);
    return false;
}

bool ViewShellChrome::handleKeyPress(QKeyEvent *event)
{
    if (!m_view || !event) {
        return false;
    }
    return m_view->hostAttention().tryKeyPressAttention(event)
        || m_view->hostCrop().tryKeyPressCrop(event)
        || m_view->hostImage().tryKeyPressZoomRegion(event)
        || m_view->hostWorkspace().tryKeyPressSelectAll(event)
        || m_view->hostImage().tryKeyPressNavigate(event)
        || m_view->hostGallery().tryKeyPressGallery(event)
        || m_view->hostWorkspace().tryKeyPressShear(event)
        || m_view->hostGallery().tryKeyPressDeleteSelection(event)
        || m_view->hostWorkspace().tryKeyPressDeleteSelection(event);
}

bool ViewShellChrome::handleMouseDoubleClick(QMouseEvent *event)
{
    if (!m_view || !event) {
        return false;
    }
    return m_view->hostImage().tryMouseDoubleClick(event)
        || m_view->hostGallery().tryMouseDoubleClick(event)
        || m_view->hostWorkspace().tryMouseDoubleClick(event);
}

void ViewShellChrome::handleLeave()
{
    if (!m_view) {
        return;
    }
    onLeave();
    m_view->hostImage().onViewportLeave();
    m_view->hostGallery().onViewportLeave();
    m_view->hostSlideshow().onViewportLeave();
}

void ViewShellChrome::handleWheel(QWheelEvent *event)
{
    if (!m_view || !event) {
        return;
    }
    if (m_view->hostGallery().tryWheelGalleryZoom(event)
        || m_view->hostGallery().tryWheelGalleryScroll(event)) {
        return;
    }
    m_view->hostImage().wheelZoomAboutCursor(event);
}

void ViewShellChrome::handleResize()
{
    if (!m_view) {
        return;
    }
    if (m_view->hostLayoutApply().active()) {
        return;
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().onViewResized();
        return;
    }
    if (m_view->isWorkspaceMode()) {
        m_view->hostDisplayPipeline().ensureWorkspaceQualityClimb();
        return;
    }
    // Image mode
    if (m_view->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    if (m_view->hostSlideshow().dwell().isMotionActive()) {
        m_view->hostSlideshow().onViewResizedDuringDwell();
        return;
    }
    m_view->hostImage().onViewResized();
}

