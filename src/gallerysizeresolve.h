// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYSIZERESOLVE_H
#define GALLERYSIZERESOLVE_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QSize>
#include <QStringList>
#include <QElapsedTimer>

class QTimer;

/**
 * Host callbacks for the Gallery packaged-layout size gate.
 * ImageView implements this; GallerySizeResolve owns pending set and progress.
 */
class GallerySizeResolveHost
{
public:
    virtual ~GallerySizeResolveHost() = default;

    /** True when ImageSizeBook already has a definitive native size for @p path. */
    virtual bool hasDefinitiveHostSize(const QString &path) const = 0;

    /** Install a known positive size into the host size book + apply to items. */
    virtual void adoptResolvedSize(const QString &path, const QSize &size) = 0;

    /**
     * Size probe failed (unsupported / unreadable). Host records failure and
     * may adopt a fixed error-cell layout size so ordered pack can proceed.
     */
    virtual void adoptSizeProbeFailed(const QString &path) = 0;

    /**
     * Queue size probes for the full cold-open set in one batch (not per-path
     * serial). Implementation should call ThumtooCache::scheduleProbeBatch.
     */
    virtual void scheduleSizeProbeBatch(const QStringList &paths) = 0;

    /** Session path order for TTFP (cover / primary first). */
    virtual QStringList sizeResolvePathOrder() const = 0;

    /**
     * True when the current Gallery layout must not pack until sizes settle
     * (packaged layouts). FreeForm returns false.
     */
    virtual bool sizeResolveLayoutDefersPopulate() const = 0;

    virtual void setSizeResolveProgress(const QString &title,
                                        const QString &detail) = 0;
    /** Clear centre HUD only when it still shows the resolving title. */
    virtual void clearSizeResolveProgress() = 0;

    /**
     * Gate completed (all probes settled: size or failure). Host packs /
     * shows placeholders for any remaining rows.
     */
    virtual void onSizeResolveGateComplete() = 0;

    /**
     * Gate cancelled without completing (mode leave / session wipe).
     * Host drops defer-populate and may clear HUD.
     */
    virtual void onSizeResolveGateCancelled() = 0;

    /**
     * A path settled (size or failure) while the gate is active.
     * Host may extend the ordered prefix of placeholders and pack in session
     * order (no random out-of-order placement).
     */
    virtual void onSizeResolvePathSettled(const QString &path) = 0;
};

/**
 * Gallery packaged-layout size gate.
 *
 * Owns pending paths and progress HUD. Does not use wall-clock timeouts —
 * each path ends in a definitive size or an explicit failure.
 * Does not own ImageView canvas state — that remains on GallerySizeResolveHost.
 */
class GallerySizeResolve : public QObject
{
    Q_OBJECT

public:
    explicit GallerySizeResolve(GallerySizeResolveHost *host,
                                QObject *parent = nullptr);

    /**
     * Inspect @p paths, adopt warm memos, batch-schedule probes for the rest.
     * @return true if the packaged-layout gate is now active (caller should
     *         defer full populate until finished(); ordered prefix may grow).
     */
    bool startIfNeeded(const QStringList &paths);

    void cancel();
    void noteProbeSettled(const QString &path, bool sizeValid);

    bool active() const { return m_active; }
    int total() const { return m_total; }
    int pendingCount() const { return m_pending.size(); }
    int failedCount() const { return m_failed; }
    int resolvedCount() const { return m_resolved; }

private:
    void finish();
    void updateProgressHud();
    void ensureProgressTimer();

    GallerySizeResolveHost *m_host = nullptr;
    bool m_active = false;
    int m_total = 0;
    int m_resolved = 0;
    int m_failed = 0;
    QSet<QString> m_pending;
    QTimer *m_progressTimer = nullptr;
    QElapsedTimer m_elapsed;

    static constexpr int kProgressIntervalMs = 100;
};

#endif // GALLERYSIZERESOLVE_H
