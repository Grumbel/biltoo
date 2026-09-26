// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTPANEL_H
#define TEXTPANEL_H

#include <QWidget>

class QEvent;

class QListView;
class QLabel;
class QCheckBox;
class QPushButton;
class TextPanelModel;
class QItemSelection;

/**
 * Side panel listing text/OCR regions for the current page layer.
 * Selection and hover are mirrored to the page overlay (and the reverse).
 */
class TextPanel : public QWidget {
    Q_OBJECT
public:
    explicit TextPanel(QWidget *parent = nullptr);

    TextPanelModel *model() const { return m_model; }

    void setLayerInfo(const QString &info);
    void setShowGlyphsChecked(bool on);
    void setShowOutlinesChecked(bool on);

    /** Programmatic selection by region indices (page → panel). */
    void setSelectedRegions(const QVector<int> &regionIndices);
    void setHoverRegion(int regionIndex);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void selectionRegionsChanged(const QVector<int> &regionIndices);
    void hoverRegionChanged(int regionIndex);
    void showGlyphsToggled(bool on);
    void showOutlinesToggled(bool on);
    void refreshRequested();

private:
    void onViewSelectionChanged();
    void onViewEntered(const QModelIndex &index);
    void onViewLeft();

    TextPanelModel *m_model = nullptr;
    QListView *m_view = nullptr;
    QLabel *m_info = nullptr;
    QCheckBox *m_glyphs = nullptr;
    QCheckBox *m_outlines = nullptr;
    bool m_blockSel = false;
};

#endif
