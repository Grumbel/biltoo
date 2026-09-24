// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Multi-mode viewport pan (owned by ViewShellChrome).

#include "view/viewshellchrome.h"
#include "imageview.h"
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
