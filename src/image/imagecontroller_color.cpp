// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Colour-grade commit debounce (owned by ImageController).

#include "image/imagecontroller.h"
#include "imageview.h"

#include <QTimer>
#include <QObject>

void ImageController::ensureColorAdjustCommitTimer()
{
    if (m_colorAdjustCommitTimer) {
        return;
    }
    m_colorAdjustCommitTimer = new QTimer(m_view);
    m_colorAdjustCommitTimer->setSingleShot(true);
    m_colorAdjustCommitTimer->setInterval(ColorAdjustCommit::kIntervalMs);
    QObject::connect(m_colorAdjustCommitTimer, &QTimer::timeout, m_view, [this]() {
        m_view->flushColorAdjustCommit();
    });
}

void ImageController::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_colorAdjustCommit.schedule(sid, path);
    ensureColorAdjustCommitTimer();
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->start();
    } else {
        m_view->flushColorAdjustCommit();
    }
}

void ImageController::stopColorAdjustCommitTimer()
{
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->stop();
    }
}
