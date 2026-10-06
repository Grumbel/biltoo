// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QObject;

namespace tilelod {

/**
 * Install the GUI-thread driver for TileScheduler: wake requests (from any
 * thread) arm one single-shot QTimer; its timeout runs TileScheduler::pump().
 * Call once at startup, after QApplication exists. The driver is parented to
 * @p parent (usually the QApplication).
 */
void installTileSchedulerQtDriver(QObject *parent);

}  // namespace tilelod
