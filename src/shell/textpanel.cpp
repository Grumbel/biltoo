// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/textpanel.h"
#include <QHash>
#include <QPair>
#include "shell/messagelogwidget.h"
#include "text/textpanelmodel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QItemSelectionModel>
#include <QEvent>
#include <QAbstractItemView>
#include <QSignalBlocker>

TextPanel::TextPanel(QWidget *parent)
    : QWidget(parent)
{
    m_model = new TextPanelModel(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_info = new QLabel(tr("No text layer"), this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(m_info);
    auto *legend = new QLabel(
        tr("Colours: text · link (layout kinds not classified yet)"), this);
    legend->setWordWrap(true);
    legend->setStyleSheet(QStringLiteral("color: palette(mid); font-size: small;"));
    legend->setToolTip(
        tr("Tesseract does not provide header/footer/page-number labels. "
           "Regions stay body until a real layout model is integrated."));
    layout->addWidget(legend);

    auto *opts = new QHBoxLayout;
    m_outlines = new QCheckBox(tr("Outlines"), this);
    m_outlines->setToolTip(tr("Draw region bounding boxes on the page"));
    m_glyphs = new QCheckBox(tr("Show text in boxes"), this);
    m_glyphs->setToolTip(
        tr("Paint the recognized text inside each region on the page "
           "(what OCR/native extraction produced)"));
    opts->addWidget(m_outlines);
    opts->addWidget(m_glyphs);
    layout->addLayout(opts);
    connect(m_outlines, &QCheckBox::toggled, this, &TextPanel::showOutlinesToggled);
    connect(m_glyphs, &QCheckBox::toggled, this, &TextPanel::showGlyphsToggled);

    auto *refresh = new QPushButton(tr("Reload layer"), this);
    refresh->setToolTip(tr("Re-fetch the text layer for the current image"));
    connect(refresh, &QPushButton::clicked, this, &TextPanel::refreshRequested);
    layout->addWidget(refresh);

    auto *speechRow = new QHBoxLayout;
    m_speakBtn = new QPushButton(tr("Speak"), this);
    m_speakBtn->setToolTip(
        tr("Read the current page from the top, or from the first selected region "
           "(local Piper TTS). Becomes Pause while speaking."));
    m_stopSpeechBtn = new QPushButton(tr("Stop"), this);
    m_stopSpeechBtn->setToolTip(tr("Stop text-to-speech"));
    m_stopSpeechBtn->setEnabled(false);
    speechRow->addWidget(m_speakBtn);
    speechRow->addWidget(m_stopSpeechBtn);
    layout->addLayout(speechRow);
    connect(m_speakBtn, &QPushButton::clicked, this, &TextPanel::speakRequested);
    connect(m_stopSpeechBtn, &QPushButton::clicked, this, &TextPanel::stopSpeechRequested);

    m_followSpeechPages = new QCheckBox(tr("Turn pages while speaking"), this);
    m_followSpeechPages->setToolTip(
        tr("When enabled, Image mode navigates to the page of the sentence currently "
           "being read. Off by default — speech continues without changing the page."));
    m_followSpeechPages->setChecked(false);
    layout->addWidget(m_followSpeechPages);
    connect(m_followSpeechPages, &QCheckBox::toggled, this, [this](bool on) {
        if (m_blockSpeechUi) {
            return;
        }
        emit followSpeechPagesToggled(on);
    });

    auto *voiceRow = new QHBoxLayout;
    voiceRow->addWidget(new QLabel(tr("Voice"), this));
    m_voiceCombo = new QComboBox(this);
    m_voiceCombo->setMinimumWidth(120);
    m_voiceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_voiceCombo->setToolTip(tr("Piper voice for this session"));
    m_voiceCombo->setEnabled(false);
    voiceRow->addWidget(m_voiceCombo, 1);
    layout->addLayout(voiceRow);
    connect(m_voiceCombo, &QComboBox::currentTextChanged, this, [this](const QString &v) {
        if (m_blockSpeechUi || v.isEmpty()) {
            return;
        }
        emit voiceChosen(v);
    });

    auto *tempoVol = new QHBoxLayout;
    tempoVol->addWidget(new QLabel(tr("Tempo"), this));
    m_speedSpin = new QDoubleSpinBox(this);
    m_speedSpin->setRange(0.5, 5.0);
    m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setDecimals(2);
    m_speedSpin->setSuffix(tr("×"));
    m_speedSpin->setValue(1.0);
    m_speedSpin->setToolTip(tr("Speech rate (Piper length_scale)"));
    tempoVol->addWidget(m_speedSpin);
    connect(m_speedSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_blockSpeechUi) {
            return;
        }
        emit speedChosen(v);
    });
    tempoVol->addWidget(new QLabel(tr("Vol"), this));
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 150);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setMinimumWidth(64);
    m_volumeSlider->setToolTip(tr("Volume 0–150% (above 100% amplifies quiet voices)"));
    tempoVol->addWidget(m_volumeSlider, 1);
    m_volumeLabel = new QLabel(tr("100%"), this);
    m_volumeLabel->setMinimumWidth(36);
    tempoVol->addWidget(m_volumeLabel);
    layout->addLayout(tempoVol);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int pct) {
        if (m_volumeLabel) {
            m_volumeLabel->setText(tr("%1%").arg(pct));
        }
        if (m_blockSpeechUi) {
            return;
        }
        emit volumeChosen(pct);
    });

    m_speechLog = new MessageLogWidget(this);
    m_speechLog->setCompact(true);
    m_speechLog->setMaximumBlockCount(80);
    m_speechLog->setPlaceholderText(tr("TTS status and errors appear here (selectable)…"));
    m_speechLog->appendInfo(tr("TTS idle"));
    layout->addWidget(m_speechLog);

    m_view = new QListView(this);
    m_view->setModel(m_model);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setMouseTracking(true);
    m_view->setUniformItemSizes(false);
    m_view->setWordWrap(true);
    layout->addWidget(m_view, 1);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &) {
                onViewSelectionChanged();
            });
    connect(m_view, &QListView::entered, this, &TextPanel::onViewEntered);
    // Viewport leave: clear hover when leaving the list.
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
}

bool TextPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_view->viewport() && event->type() == QEvent::Leave) {
        onViewLeft();
    }
    return QWidget::eventFilter(obj, event);
}

void TextPanel::setLayerInfo(const QString &info)
{
    if (m_info) {
        m_info->setText(info.isEmpty() ? tr("No text layer") : info);
    }
}

void TextPanel::setLayer(const ThumtooCache::PageTextLayer &layer)
{
    if (m_model) {
        m_model->setLayer(layer);
    }
}

void TextPanel::setMemberLayers(const QVector<TextPanelModel::MemberLayer> &members)
{
    if (m_model) {
        m_model->setMemberLayers(members);
    }
}

void TextPanel::clearLayer()
{
    if (m_model) {
        m_model->clear();
    }
}

void TextPanel::setShowGlyphsChecked(bool on)
{
    if (m_glyphs) {
        QSignalBlocker b(m_glyphs);
        m_glyphs->setChecked(on);
    }
}

void TextPanel::setShowOutlinesChecked(bool on)
{
    if (m_outlines) {
        QSignalBlocker b(m_outlines);
        m_outlines->setChecked(on);
    }
}

void TextPanel::setSpeechStatus(const QString &text, bool isError)
{
    if (!m_speechLog) {
        return;
    }
    const QString msg = text.isEmpty() ? tr("TTS idle") : text;
    if (isError) {
        m_speechLog->appendError(msg);
    } else {
        // Keep a short running log of state changes (not only the last line).
        m_speechLog->appendInfo(msg);
    }
}

void TextPanel::setSpeechBusy(bool speaking)
{
    setSpeechPlaybackState(speaking, false);
}

void TextPanel::setSpeechPlaybackState(bool speaking, bool paused)
{
    if (m_stopSpeechBtn) {
        m_stopSpeechBtn->setEnabled(speaking);
    }
    if (!m_speakBtn) {
        return;
    }
    if (!speaking) {
        m_speakBtn->setText(tr("Speak"));
        m_speakBtn->setToolTip(
            tr("Read the current page from the top, or from the first selected region"));
    } else if (paused) {
        m_speakBtn->setText(tr("Resume"));
        m_speakBtn->setToolTip(tr("Continue text-to-speech"));
    } else {
        m_speakBtn->setText(tr("Pause"));
        m_speakBtn->setToolTip(tr("Pause text-to-speech"));
    }
}

void TextPanel::setSpeakEnabled(bool on)
{
    if (m_speakBtn) {
        m_speakBtn->setEnabled(on);
    }
}

void TextPanel::setMultiSelection(const TextSelection &selection)
{
    if (!m_view || !m_model) {
        return;
    }
    m_blockSel = true;
    QItemSelection sel;
    QVector<int> primaryOnly;
    for (const TextSelRef &ref : selection.refs()) {
        const int row = m_model->rowForRegion(ref.sessionId, ref.regionIndex);
        if (row < 0) {
            continue;
        }
        const QModelIndex idx = m_model->index(row, 0);
        sel.select(idx, idx);
        if (!m_model->isMultiPage() || ref.sessionId == m_model->sessionIdAt(row)) {
            primaryOnly.append(ref.regionIndex);
        }
    }
    m_view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    if (!selection.isEmpty()) {
        const int row = m_model->rowForRegion(selection.refs().first().sessionId,
                                              selection.refs().first().regionIndex);
        if (row >= 0) {
            m_view->scrollTo(m_model->index(row, 0), QAbstractItemView::EnsureVisible);
        }
    }
    m_lastEmittedSelection = primaryOnly;
    m_blockSel = false;
}

void TextPanel::setSelectedRegions(const QVector<int> &regionIndices)
{
    if (!m_view || !m_model) {
        return;
    }
    // Avoid selection churn when page→panel sync repeats the same set.
    if (regionIndices == m_lastEmittedSelection) {
        const auto rows = m_view->selectionModel()->selectedRows();
        QVector<int> current;
        current.reserve(rows.size());
        for (const QModelIndex &idx : rows) {
            const int ri = m_model->regionIndexAt(idx.row());
            if (ri >= 0) {
                current.append(ri);
            }
        }
        if (current == regionIndices) {
            return;
        }
    }
    m_blockSel = true;
    QItemSelection sel;
    for (int ri : regionIndices) {
        const int row = m_model->rowForRegion(ri);
        if (row < 0) {
            continue;
        }
        const QModelIndex idx = m_model->index(row, 0);
        sel.select(idx, idx);
    }
    m_view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    if (!regionIndices.isEmpty()) {
        const int row = m_model->rowForRegion(regionIndices.first());
        if (row >= 0) {
            m_view->scrollTo(m_model->index(row, 0), QAbstractItemView::EnsureVisible);
        }
    }
    m_lastEmittedSelection = regionIndices;
    m_blockSel = false;
}

void TextPanel::setHoverRegion(int regionIndex)
{
    if (!m_view || !m_model || regionIndex < 0) {
        return;
    }
    const int row = m_model->rowForRegion(regionIndex);
    if (row < 0) {
        return;
    }
    // Scroll only. Do NOT setCurrentIndex: on ExtendedSelection that replaces
    // the item selection and clears multi-select when the pointer moves over
    // the list (hover sync from the page or entered()).
    m_view->scrollTo(m_model->index(row, 0), QAbstractItemView::EnsureVisible);
}

void TextPanel::onViewSelectionChanged()
{
    if (m_blockSel || !m_view || !m_model) {
        return;
    }
    const auto rows = m_view->selectionModel()->selectedRows();
    if (m_model->isMultiPage()) {
        TextSelection bag;
        // Preserve list order (already reading order across members).
        QVector<QPair<SessionImageId, QVector<int>>> bySid;
        QVector<SessionImageId> sidOrder;
        QHash<SessionImageId, QVector<int>> map;
        QHash<SessionImageId, QVector<QString>> texts;
        for (const QModelIndex &idx : rows) {
            const int row = idx.row();
            const int ri = m_model->regionIndexAt(row);
            const SessionImageId sid = m_model->sessionIdAt(row);
            if (ri < 0 || sid == kInvalidSessionImageId) {
                continue;
            }
            if (!map.contains(sid)) {
                sidOrder.append(sid);
            }
            map[sid].append(ri);
            const QString tx = m_model->data(idx, TextPanelModel::TextRole).toString();
            texts[sid].append(tx);
        }
        for (SessionImageId sid : sidOrder) {
            bag.setForSession(sid, map.value(sid), texts.value(sid));
        }
        emit selectionMultiChanged(bag);
        QVector<int> primary;
        if (!sidOrder.isEmpty()) {
            primary = map.value(sidOrder.first());
        }
        m_lastEmittedSelection = primary;
        return;
    }
    QVector<int> regions;
    regions.reserve(rows.size());
    for (const QModelIndex &idx : rows) {
        const int ri = m_model->regionIndexAt(idx.row());
        if (ri >= 0) {
            regions.append(ri);
        }
    }
    if (regions == m_lastEmittedSelection) {
        return;
    }
    m_lastEmittedSelection = regions;
    emit selectionRegionsChanged(regions);
}

void TextPanel::onViewEntered(const QModelIndex &index)
{
    if (!m_model || !index.isValid()) {
        return;
    }
    const int ri = m_model->regionIndexAt(index.row());
    if (ri == m_lastHoverRegion) {
        return;
    }
    m_lastHoverRegion = ri;
    emit hoverRegionChanged(ri);
}

void TextPanel::onViewLeft()
{
    if (m_lastHoverRegion == -1) {
        return;
    }
    m_lastHoverRegion = -1;
    emit hoverRegionChanged(-1);
}

void TextPanel::setVoices(const QStringList &voices, const QString &current)
{
    if (!m_voiceCombo) {
        return;
    }
    m_blockSpeechUi = true;
    m_voiceCombo->clear();
    m_voiceCombo->addItems(voices);
    m_voiceCombo->setEnabled(!voices.isEmpty());
    if (!current.isEmpty()) {
        const int idx = m_voiceCombo->findText(current);
        if (idx >= 0) {
            m_voiceCombo->setCurrentIndex(idx);
        }
    }
    m_blockSpeechUi = false;
}

void TextPanel::setSpeed(double speed)
{
    if (!m_speedSpin) {
        return;
    }
    m_blockSpeechUi = true;
    m_speedSpin->setValue(speed);
    m_blockSpeechUi = false;
}

void TextPanel::setVolumePercent(int percent)
{
    if (!m_volumeSlider) {
        return;
    }
    m_blockSpeechUi = true;
    m_volumeSlider->setValue(qBound(0, percent, 150));
    if (m_volumeLabel) {
        m_volumeLabel->setText(tr("%1%").arg(m_volumeSlider->value()));
    }
    m_blockSpeechUi = false;
}

void TextPanel::setFollowSpeechPagesChecked(bool on)
{
    if (!m_followSpeechPages) {
        return;
    }
    m_blockSpeechUi = true;
    m_followSpeechPages->setChecked(on);
    m_blockSpeechUi = false;
}

bool TextPanel::followSpeechPagesChecked() const
{
    return m_followSpeechPages && m_followSpeechPages->isChecked();
}
