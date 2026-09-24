// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DUALIMAGESHELL_H
#define DUALIMAGESHELL_H

#include "imageview_types.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class ImageView;
class QSplitter;
class SessionDocument;
class SessionSeedBook;

/**
 * Dual ImageView Stage 2c.2–2c.3 shell: primary host (session owner) plus an
 * optional secondary compare surface that shares ItemWorld + DisplayPipeline.
 *
 * MainWindow keeps talking to primary() for Gallery/Workspace/session chrome.
 * Focus → setActiveHost on the shared pipeline.
 *
 * Stage 2c.3: openOnSecondary + navigateSecondary (independent compare nav).
 * Secondary is destroyed on disable without tearing down the shared pipeline.
 */
class DualImageShell : public QWidget
{
    Q_OBJECT
public:
    explicit DualImageShell(ImageView *primary, QWidget *parent = nullptr);

    ImageView *primary() const { return m_primary; }
    ImageView *secondary() const { return m_secondary; }
    ImageView *activeView() const { return m_active ? m_active : m_primary; }

    bool isDualEnabled() const { return m_dual; }
    bool isSecondaryActive() const
    {
        return m_dual && m_secondary && m_active == m_secondary;
    }

    SessionImageId secondarySessionId() const { return m_secondarySessionId; }
    QString secondaryPath() const { return m_secondaryPath; }

    void setDualEnabled(bool on, SessionDocument *sessionDoc, SessionSeedBook *seedBook);

    /**
     * Load @p path / @p sid on the secondary surface (Image mode).
     * Switches active host to secondary so the shared pipeline installs there.
     */
    void openOnSecondary(const QString &path, SessionImageId sid);

    /**
     * Session-relative navigation for the secondary pane only (wraps).
     * @return true if a load was issued.
     */
    bool navigateSecondary(int delta, const QStringList &paths,
                           const QVector<SessionImageId> &ids);

    void noteFocus(ImageView *view);

signals:
    void dualEnabledChanged(bool on);
    void activeViewChanged(ImageView *view);
    void secondarySessionChanged(SessionImageId id, const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void ensureSecondary(SessionDocument *sessionDoc, SessionSeedBook *seedBook);
    void destroySecondary();

    ImageView *m_primary = nullptr;
    ImageView *m_secondary = nullptr;
    ImageView *m_active = nullptr;
    QSplitter *m_splitter = nullptr;
    bool m_dual = false;
    SessionImageId m_secondarySessionId = kInvalidSessionImageId;
    QString m_secondaryPath;
};

#endif
