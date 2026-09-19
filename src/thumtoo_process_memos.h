// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMTOO_PROCESS_MEMOS_H
#define THUMTOO_PROCESS_MEMOS_H

#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>

#include <mutex>

namespace ThumtooCache {

/**
 * Process-lifetime path memos for sizes and durable-tile discovery.
 *
 * Thread-safe. GUI may read size / durable-known without Store I/O.
 * Session Open/archive replace clears durable memos (not size — sizes remain
 * valid for the same path string across sessions when the file is unchanged).
 *
 * Owned separately from pixel queues and the serial size-probe FIFO so those
 * subsystems can evolve without sharing ad-hoc globals.
 */
class ProcessMemos
{
public:
    static ProcessMemos &instance();

    ProcessMemos(const ProcessMemos &) = delete;
    ProcessMemos &operator=(const ProcessMemos &) = delete;

    // --- size (process memo; GUI-safe peek) ---------------------------------

    /** Empty if unknown. Does not open the Store. */
    QSize size(const QString &path) const;
    void noteSize(const QString &path, const QSize &size);

    // --- durable tiles -------------------------------------------------------

    bool durableYes(const QString &path) const;
    /** Finest scale for a durableYes path; 0 if unknown / not yes. */
    int durableMinScale(const QString &path) const;
    void noteDurableYes(const QString &path, int minScale);
    /**
     * True if a negative memo is still in force (do not re-query Store yet).
     * @p nowMs is QDateTime::currentMSecsSinceEpoch().
     */
    bool durableNoActive(const QString &path, qint64 nowMs) const;
    void noteDurableNo(const QString &path, qint64 untilMs);
    void clearDurableNo(const QString &path);
    /**
     * Drop durable yes/no/min_scale for one path (hard reload / Store purge).
     * Size memo is retained unless the caller also wants a size re-probe.
     */
    void clearDurablePath(const QString &path);

    /**
     * Session Open / archive replace: drop durable yes/no/min_scale so a new
     * path set cannot inherit coverage from the previous archive.
     * Size memos are intentionally retained.
     */
    void clearSessionReplaceDurable();

private:
    ProcessMemos() = default;

    mutable std::mutex m_mu;
    QHash<QString, QSize> m_size;
    QSet<QString> m_durableYes;
    QHash<QString, int> m_durableMinScale;
    QHash<QString, qint64> m_durableNoUntilMs;
};

} // namespace ThumtooCache

#endif // THUMTOO_PROCESS_MEMOS_H
