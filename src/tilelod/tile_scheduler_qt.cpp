// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_scheduler_qt.hpp"

#include "tilelod/core.hpp"
#include <thumtoo/lod/tile_scheduler.hpp>

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace tilelod {

namespace {

class TileSchedulerDriver : public QObject {
public:
  explicit TileSchedulerDriver(QObject *parent)
      : QObject(parent)
  {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, this,
                     []() { TileScheduler::instance().pump(); });
  }

  /// GUI thread: arm the timer unless an earlier wake is already pending.
  void arm(qint64 delay_ms)
  {
    int const d = static_cast<int>(
        std::clamp<qint64>(delay_ms, 0, std::numeric_limits<int>::max() / 2));
    if (m_timer.isActive() && m_timer.remainingTime() <= d) {
      return;
    }
    m_timer.start(d);
  }

private:
  QTimer m_timer;
};

}  // namespace

void installTileSchedulerQtDriver(QObject *parent)
{
  // thumtoo's scheduler traces with Config::trace (or THUMTOO_LOD_DEBUG);
  // keep BILTOO_TILE_DEBUG switching it on as before.
  TileScheduler::Config cfg = TileScheduler::instance().config();
  char const* td = std::getenv("BILTOO_TILE_DEBUG");
  cfg.trace = cfg.trace || (td && td[0] && td[0] != '0');
  TileScheduler::instance().set_config(cfg);

  auto *driver = new TileSchedulerDriver(parent);
  QPointer<TileSchedulerDriver> guard(driver);
  TileScheduler::instance().set_wake_hook([guard](std::int64_t delay_ms) {
    TileSchedulerDriver *d = guard.data();
    if (!d) {
      return;
    }
    if (QThread::currentThread() == d->thread()) {
      d->arm(delay_ms);
      return;
    }
    QMetaObject::invokeMethod(
        d, [guard, delay_ms]() {
          if (guard) {
            guard->arm(delay_ms);
          }
        },
        Qt::QueuedConnection);
  });
}

}  // namespace tilelod
