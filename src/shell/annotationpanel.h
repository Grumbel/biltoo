// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONPANEL_H
#define ANNOTATIONPANEL_H

#include "imageview_types.h"

#include <QColor>
#include <QVector>
#include <QWidget>

class QLabel;
class QSlider;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QListWidget;

/**
 * One row in the annotated-pages list (session pages that already have marks).
 */
struct AnnotationPageEntry {
    SessionImageId sid = kInvalidSessionImageId;
    int sessionIndex = -1; ///< 0-based index in the current session list
    QString label;         ///< Display name (path / page)
    int objectCount = 0;
};

/**
 * Side panel for annotation colour, stroke width, layer visibility, and a list
 * of pages that already have annotations (jump to page on activation).
 * Does not own tools (toolbar / Image menu); mirrors AnnotationController state.
 */
class AnnotationPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AnnotationPanel(QWidget *parent = nullptr);

    void setColor(const QColor &c);
    QColor color() const { return m_color; }

    void setWidth(qreal w);
    qreal width() const { return m_width; }

    void setLayerVisible(bool on);
    bool layerVisible() const;

    /** Optional status line (e.g. active tool name). */
    void setStatusText(const QString &text);

    /**
     * Replace the annotated-pages list. @p currentSid is highlighted when present.
     * Empty @p entries shows a short placeholder.
     */
    void setAnnotatedPages(const QVector<AnnotationPageEntry> &entries,
                           SessionImageId currentSid = kInvalidSessionImageId);
    void setMarkSelectionEnabled(bool on);

signals:
    void colorChanged(const QColor &c);
    void widthChanged(qreal w);
    void layerVisibleChanged(bool on);
    /** User activated a row in the annotated-pages list. */
    void jumpToSessionId(SessionImageId sid);
    /** Convert text selection → highlight quads (panel colour). */
    void markSelectionRequested();

private:
    void rebuildSwatch();
    void emitColor(const QColor &c);
    void onWidthSlider(int value);
    void onWidthSpin(double value);
    void pickCustomColor();
    void onPageActivated();

    QColor m_color = QColor(246, 211, 45);
    qreal m_width = 18.0;
    bool m_block = false;

    QLabel *m_swatch = nullptr;
    QLabel *m_status = nullptr;
    QSlider *m_widthSlider = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QCheckBox *m_visibleCheck = nullptr;
    QPushButton *m_customColorBtn = nullptr;
    QPushButton *m_markSelectionBtn = nullptr;
    QListWidget *m_pageList = nullptr;
    QLabel *m_pageListHint = nullptr;
};

#endif // ANNOTATIONPANEL_H
