// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Multi-mode viewport pan (owned by ViewShellChrome).

#include "view/viewshellchrome.h"
#include "imageview.h"
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
            if (m_hud.perf().isEnabled()) {
                topLeft.append({
                    QCoreApplication::translate("ImageView", "FPS %1 · paint %2 ms · decode-win %3 ms (max %4)")
                        .arg(m_hud.perf().fpsValue(), 0, 'f', 1)
                        .arg(m_hud.perf().lastPaintUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_hud.perf().lastDecodeWindowUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_hud.perf().maxDecodeWindowUsValue() / 1000.0, 0, 'f', 1),
                    false});
            }
            ImageItem *focus = targetItem();
            if (!focus) {
                focus = primaryItem();
            }
            if (focus) {
                const QString q = pixelQualityLabel(focus);
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

