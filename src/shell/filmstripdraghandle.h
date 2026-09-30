// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILMSTRIPDRAGHANDLE_H
#define FILMSTRIPDRAGHANDLE_H

#include <QWidget>

namespace KDDockWidgets::QtWidgets {
class DockWidget;
}

/**
 * Filmstrip dock grip (title bar is collapsed).
 * Horizontal strip (top/bottom dock): grip on the left.
 * Vertical strip (left/right dock): grip on the top.
 */
class FilmstripDragHandle final : public QWidget
{
    Q_OBJECT
public:
    explicit FilmstripDragHandle(KDDockWidgets::QtWidgets::DockWidget *dock,
                                 QWidget *parent = nullptr);

    /** false = left edge (vertical handle); true = top edge (horizontal handle). */
    void setAlongTop(bool alongTop);
    bool alongTop() const { return m_alongTop; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    KDDockWidgets::QtWidgets::DockWidget *m_dock = nullptr;
    bool m_hovered = false;
    bool m_alongTop = false;
};

#endif
