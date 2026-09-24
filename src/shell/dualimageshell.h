// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DUALIMAGESHELL_H
#define DUALIMAGESHELL_H

#include <QWidget>

class ImageView;
class QSplitter;
class SessionDocument;
class SessionSeedBook;

/**
 * Dual ImageView Stage 2c.2 shell: one primary host (session owner) plus an
 * optional secondary compare surface that shares ItemWorld + DisplayPipeline.
 *
 * MainWindow keeps talking to primary() for Gallery/Workspace/session chrome.
 * When dual is on, focus on either pane calls setActiveHost on the shared
 * pipeline so PreferCache / tile ticks target the focused surface.
 *
 * Secondary is destroyed on disable (does not tear down the shared pipeline).
 */
class DualImageShell : public QWidget
{
    Q_OBJECT
public:
    explicit DualImageShell(ImageView *primary, QWidget *parent = nullptr);

    ImageView *primary() const { return m_primary; }
    ImageView *secondary() const { return m_secondary; }
    /** Focused pane when dual; otherwise primary. */
    ImageView *activeView() const { return m_active ? m_active : m_primary; }

    bool isDualEnabled() const { return m_dual; }

    /**
     * Enable dual compare: create secondary, bind shared ItemWorld + pipeline,
     * show splitter. @p sessionDoc / @p seedBook are bound on the secondary the
     * same way MainWindow binds the primary.
     */
    void setDualEnabled(bool on, SessionDocument *sessionDoc, SessionSeedBook *seedBook);

    /** Record focus and switch pipeline active host (GUI thread). */
    void noteFocus(ImageView *view);

signals:
    void dualEnabledChanged(bool on);
    void activeViewChanged(ImageView *view);

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
};

#endif
