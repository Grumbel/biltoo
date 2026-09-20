// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Empty-session invite and HUD panel paint.

#include "imageview.h"
#include "hudmodel.h"
#include "viewtransform.h"

#include <QPainter>
#include "hudgeometry.h"
#include "imageitem.h"

void ImageView::paintEmptySessionInvite(QPainter &painter)
{
    // Empty session: invite the user to open or drop images.
    // Suppress while centre progress is active (archive expand / size resolve).
    if (m_items.isEmpty() && !hasClassicPath() && !m_cropCtrl.session().active()
        && m_centreProgress.titleRef().isEmpty() && !gallerySizeResolveActive()) {
        painter.save();
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont titleFont = font();
        titleFont.setPointSize(HudGeometry::clampTitlePointSize(titleFont.pointSize()));
        titleFont.setBold(true);
        QFont hintFont = font();
        hintFont.setPointSize(HudGeometry::clampHintPointSize(hintFont.pointSize()));
        const QString title = isWorkspaceMode()
            ? tr("Drop images here")
            : tr("Drop images here or open a file");
        const QString hint = isWorkspaceMode()
            ? tr("Drag files or filmstrip thumbnails onto the canvas to place images")
            : tr("File → Open…  ·  Ctrl+O  ·  drag and drop");
        const QFontMetrics titleFm(titleFont);
        const QFontMetrics hintFm(hintFont);
        const int gap = 8;
        const int totalH = titleFm.height() + gap + hintFm.height();
        const int cy = viewport()->height() / 2 - totalH / 2;
        painter.setFont(titleFont);
        painter.setPen(QColor(220, 220, 220, 230));
        painter.drawText(QRect(0, cy, viewport()->width(), titleFm.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter, title);
        painter.setFont(hintFont);
        painter.setPen(QColor(180, 180, 180, 200));
        painter.drawText(QRect(0, cy + titleFm.height() + gap, viewport()->width(),
                               hintFm.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter, hint);

        // Edge-zone captions (Image mode uses these corners once a session is open).
        if (isImageMode() || (!isWorkspaceMode() && !isGalleryMode())) {
            QFont edgeFont = font();
            edgeFont.setPointSize(HudGeometry::clampEdgePointSize(edgeFont.pointSize()));
            painter.setFont(edgeFont);
            painter.setPen(QColor(160, 160, 160, 180));
            const QFontMetrics efm(edgeFont);
            const int em = 16;
            const int vw = viewport()->width();
            const int vh = viewport()->height();
            const QString prevLabel = tr("← Previous");
            const QString nextLabel = tr("Next →");
            const QString backLabel = tr("↑ Back to Gallery / Workspace");
            painter.drawText(QRect(em, vh / 2 - efm.height() / 2, efm.horizontalAdvance(prevLabel) + 8,
                                   efm.height()),
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

}


void ImageView::paintHudPanels(QPainter &painter)
{
    // HUD layout:
    //   top-left  — transient actions (slideshow, fit, …), never Next/Prev
    //   top-right — session index [i/n]
    //   bottom    — filename (+ technical detail when the HUD is pinned)
    // Crop mode: always show a pinned “Crop mode” cue so the tool state is clear.
    const QString ssPrefetchLine = m_slideshow.slideshowPrefetchHudLine();
    // Loading · … only in the extended (pinned) HUD — not as a free-floating
    // chip during slideshow or normal Image browsing.
    const QString loadingLine = m_hudPrefs.isVisible() ? loadingStatusHudLine() : QString();
    if (m_cropCtrl.session().active() || m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_hudFlash.isIdentityPulse()
        || m_slideshow.hud().isPausedHud() || gallerySizeResolveActive()
        || !m_centreProgress.titleRef().isEmpty()
        || !ssPrefetchLine.isEmpty()
        || !m_gallery.hoverPath().isEmpty()) {
        // Prefer the user preference (Preferences → HUD), not the widget font.
        QFont f = font();
        const int pt = m_hudPrefs.effectiveFontPointSize();
        f.setPointSize(pt);
        QFont boldF = f;
        boldF.setBold(true);
        const QFontMetrics fm(f);
        const QFontMetrics fmBold(boldF);
        const int margin = 10;
        const int pad = 8;
        const int lineGap = 2;
        const int viewW = viewport()->width();
        const int viewH = viewport()->height();

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
            painter.setBrush(m_hudPrefs.effectivePanelColor());
            painter.drawRoundedRect(bg, 6, 6);
            painter.setPen(m_hudPrefs.effectiveTextColor());
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
        if (m_cropCtrl.session().active()) {
            drawPanel({{tr("Crop mode"), true},
                       {tr("Handles · Reset · Apply · Esc"), false}},
                      margin, margin, false, false);
        } else if (m_slideshow.hud().isPausedHud()) {
            drawPanel({{tr("❚❚  Paused"), true},
                       {tr("Space: resume · Esc: leave"), false}},
                      margin, margin, false, false);
        } else if (!m_centreProgress.titleRef().isEmpty()) {
            QList<HudLine> lines;
            lines.append({m_centreProgress.titleRef(), true});
            if (!m_centreProgress.detailRef().isEmpty()) {
                lines.append({m_centreProgress.detailRef(), false});
            }
            drawPanel(lines, 0, 0, false, false, true);
        } else if (gallerySizeResolveActive() && m_gallerySizeResolve.total() > 0) {
            // Fallback if title was cleared but gate still active.
            const int done = ViewTransform::nonNeg(
                qint64(m_gallerySizeResolve.total())
                - qint64(m_gallerySizeResolve.pendingCount()));
            drawPanel({{tr("Resolving sizes…"), true},
                       {tr("%1 / %2").arg(done).arg(m_gallerySizeResolve.total()), false}},
                      0, 0, false, false, true);
        } else if (m_hudFlash.isVisible() && m_hudFlash.hasAction()) {
            QString actionLine = m_hudFlash.actionText();
            if (m_hudFlash.hasDetail()) {
                actionLine += QLatin1Char(' ') + m_hudFlash.detailText();
            }
            drawPanel({{actionLine, true}}, margin, margin, false, false);
        } else if (m_hudPrefs.isVisible() || m_hudFlash.isIdentityPulse()) {
            QList<HudLine> topLeft;
            if (!loadingLine.isEmpty()) {
                topLeft.append({loadingLine, true});
            }
            if (!ssPrefetchLine.isEmpty()) {
                topLeft.append({ssPrefetchLine, false});
            }
            if (m_perf.isEnabled()) {
                topLeft.append({
                    tr("FPS %1 · paint %2 ms · decode-win %3 ms (max %4)")
                        .arg(m_perf.fpsValue(), 0, 'f', 1)
                        .arg(m_perf.lastPaintUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_perf.lastDecodeWindowUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_perf.maxDecodeWindowUsValue() / 1000.0, 0, 'f', 1),
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
                        ? tr("%1 · %2px").arg(q).arg(edge)
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
        const QString badge = m_slideshow.sessionBadgeText();
        if (!badge.isEmpty() && (m_hudPrefs.isVisible() || m_hudFlash.isIdentityPulse())) {
            drawPanel({{badge, true}}, 0, margin, true, false);
        }

        // Bottom: filename — pinned HUD, identity pulse after user nav, or gallery hover
        if (m_hudPrefs.isVisible() || m_hudFlash.isIdentityPulse() || !m_gallery.hoverPath().isEmpty()) {
            QList<HudLine> bottom;
            const QString name = hudFileName();
            if (!name.isEmpty()) {
                bottom.append({name, true});
            }
            if (m_hudPrefs.isVisible()) {
                const QString tech = statusText();
                if (!tech.isEmpty() && tech != name) {
                    bottom.append({tech, false});
                }
            }
            drawPanel(bottom, margin, 0, false, true);
        }
    }


}

