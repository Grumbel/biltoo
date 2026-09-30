// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/filmstripdraghandle.h"

#include <kddockwidgets/qtwidgets/DockWidget.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

namespace {
constexpr int kHandleWidth = 10;
} // namespace

FilmstripDragHandle::FilmstripDragHandle(KDDockWidgets::QtWidgets::DockWidget *dock,
                                         QWidget *parent)
    : QWidget(parent)
    , m_dock(dock)
{
    setObjectName(QStringLiteral("FilmstripDragHandle"));
    setCursor(Qt::SizeAllCursor);
    setToolTip(tr("Drag to move the filmstrip"));
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(kHandleWidth);
}

QSize FilmstripDragHandle::sizeHint() const
{
    return {kHandleWidth, 40};
}

QSize FilmstripDragHandle::minimumSizeHint() const
{
    return {kHandleWidth, 24};
}

void FilmstripDragHandle::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor base = palette().color(QPalette::Window);
    const QColor mid = palette().color(QPalette::Mid);
    const QColor mark = m_hovered ? palette().color(QPalette::Highlight) : mid;

    p.fillRect(rect(), base.darker(m_hovered ? 108 : 103));

    // Vertical grip: three pairs of dots centred in the strip.
    const int cx = width() / 2;
    const int cy = height() / 2;
    const int gap = 5;
    p.setPen(Qt::NoPen);
    p.setBrush(mark);
    for (int row = -1; row <= 1; ++row) {
        const int y = cy + row * gap;
        p.drawEllipse(QPointF(cx - 1.5, y), 1.3, 1.3);
        p.drawEllipse(QPointF(cx + 1.5, y), 1.3, 1.3);
    }

    // Subtle edge line toward the thumbnails.
    p.setPen(QPen(mid, 1));
    p.drawLine(width() - 1, 0, width() - 1, height());
}

void FilmstripDragHandle::mousePressEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton && m_dock) {
        // Same path as dragging a normal dock title bar (KDDW example).
        if (KDDockWidgets::Core::DockWidget *core = m_dock->dockWidget()) {
            core->startDragging(/*byTab=*/false);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void FilmstripDragHandle::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void FilmstripDragHandle::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
