// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYSIZERESOLVE_H
#define GALLERYSIZERESOLVE_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QSize>

class QTimer;

/**
 * Host surface for Gallery cold-open size gating.
 *
 * ImageView implements this; GallerySizeResolve owns timers, pending set, and
 * progress text policy. Canvas pack / placeholder creation stay on the host.
 */
class GallerySizeResolveHost
{
public:
    virtual ~GallerySizeResolveHost() = default;

    /** Host map already has a non-provisional size for @p path. */
    virtual bool hasDefinitiveHostSize(const QString &path) const = 0;

    /**
     * Install a process-memo / probe size into the host map and live geometry.
     * Called from the GUI thread only.
     */
    virtual void adoptResolvedSize(const QString &path, const QSize &size) = 0;

    /** Enqueue an async size probe (Thumtoo scheduleProbe or equivalent). */
    virtual void scheduleSizeProbe(const QString &path) = 0;

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
     * Gate completed (all probes settled or safety timeout). Host packs /
     * shows placeholders. @p wasActive is always true when invoked from finish.
     */
    virtual void onSizeResolveGateComplete() = 0;

    /**
     * Gate cancelled without completing (mode leave / session wipe).
     * Host drops defer-populate and may clear HUD.
     */
    virtual void onSizeResolveGateCancelled() = 0;
};

/**
 * Gallery packaged-layout size gate.
 *
 * Owns pending paths, safety timeout, and progress tick (memo sweep). Does not
 * own ImageView canvas state — that remains on GallerySizeResolveHost.
 */
class GallerySizeResolve : public QObject
{
    Q_OBJECT

public:
    explicit GallerySizeResolve(GallerySizeResolveHost *host,
                                QObject *parent = nullptr);

    /**
     * Inspect @p paths, adopt warm memos, schedule probes for the rest.
     * @return true if the packaged-layout gate is now active (caller should
     *         defer populate until finished()).
     */
    bool startIfNeeded(const QStringList &paths);

    void cancel();
    void noteProbeSettled(const QString &path);

    bool active() const { return m_active; }
    int total() const { return m_total; }
    int pendingCount() const { return m_pending.size(); }

private:
    void finish();
    void updateProgressHud();
    void ensureTimers();

    GallerySizeResolveHost *m_host = nullptr;
    bool m_active = false;
    int m_total = 0;
    QSet<QString> m_pending;
    QTimer *m_safetyTimer = nullptr;
    QTimer *m_progressTimer = nullptr;

    static constexpr int kSafetyTimeoutMs = 45000;
    static constexpr int kProgressIntervalMs = 50;
};

#endif // GALLERYSIZERESOLVE_H
