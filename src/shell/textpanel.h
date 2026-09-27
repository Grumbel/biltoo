// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTPANEL_H
#define TEXTPANEL_H

#include "host/thumtoocache.h"

#include <QWidget>
#include <QVector>

class QEvent;
class QListView;
class QLabel;
class QCheckBox;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QSlider;
class TextPanelModel;
class QItemSelection;
class QModelIndex;

/**
 * Side panel listing text/OCR regions for the current page layer.
 * Selection and hover are mirrored to the page overlay (and the reverse).
 * Speak / Stop and voice / tempo / volume drive local Piper TTS.
 */
class TextPanel : public QWidget {
    Q_OBJECT
public:
    explicit TextPanel(QWidget *parent = nullptr);

    TextPanelModel *model() const { return m_model; }

    void setLayerInfo(const QString &info);
    void setLayer(const ThumtooCache::PageTextLayer &layer);
    void clearLayer();
    void setShowGlyphsChecked(bool on);
    void setShowOutlinesChecked(bool on);

    void setSelectedRegions(const QVector<int> &regionIndices);
    void setHoverRegion(int regionIndex);

    void setSpeechStatus(const QString &text);
    void setSpeechBusy(bool speaking);
    void setSpeakEnabled(bool on);

    void setVoices(const QStringList &voices, const QString &current);
    void setSpeed(double speed);
    void setVolumePercent(int percent); // 0..150

signals:
    void selectionRegionsChanged(const QVector<int> &regionIndices);
    void hoverRegionChanged(int regionIndex);
    void showGlyphsToggled(bool on);
    void showOutlinesToggled(bool on);
    void refreshRequested();
    void speakRequested();
    void stopSpeechRequested();
    void voiceChosen(const QString &voice);
    void speedChosen(double speed);
    void volumeChosen(int percent); // 0..150

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void onViewSelectionChanged();
    void onViewEntered(const QModelIndex &index);
    void onViewLeft();

    TextPanelModel *m_model = nullptr;
    QListView *m_view = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_speechStatus = nullptr;
    QCheckBox *m_glyphs = nullptr;
    QCheckBox *m_outlines = nullptr;
    QPushButton *m_speakBtn = nullptr;
    QPushButton *m_stopSpeechBtn = nullptr;
    QComboBox *m_voiceCombo = nullptr;
    QDoubleSpinBox *m_speedSpin = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QLabel *m_volumeLabel = nullptr;
    bool m_blockSel = false;
    bool m_blockSpeechUi = false;
    int m_lastHoverRegion = -2;
    QVector<int> m_lastEmittedSelection;
};

#endif
