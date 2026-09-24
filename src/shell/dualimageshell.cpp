// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/dualimageshell.h"

#include "imageview.h"
#include "display/displaypipelinecontroller.h"
#include "session/sessiondocument.h"
#include "session/sessionseedbook.h"
#include "util/biltoo_thread.h"

#include <QHBoxLayout>
#include <QEvent>
#include <QSplitter>

DualImageShell::DualImageShell(ImageView *primary, QWidget *parent)
    : QWidget(parent)
    , m_primary(primary)
    , m_active(primary)
{
    Q_ASSERT(m_primary);
    m_primary->setParent(this);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_primary);
    lay->addWidget(m_splitter);

    m_primary->installEventFilter(this);
}

void DualImageShell::ensureSecondary(SessionDocument *sessionDoc, SessionSeedBook *seedBook)
{
    if (m_secondary) {
        return;
    }
    m_secondary = new ImageView(this);
    m_secondary->setAccessibleName(tr("Compare image view"));
    m_secondary->setAccessibleDescription(
        tr("Secondary Image-mode surface for side-by-side compare. Shares "
           "appearance and display pipeline with the primary view."));
    m_secondary->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_secondary->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_secondary->setMinimumHeight(120);

    if (sessionDoc) {
        m_secondary->bindSessionDocument(sessionDoc);
    }
    if (seedBook) {
        m_secondary->bindSessionSeedBook(seedBook);
    }

    m_secondary->bindSharedItemWorld(&m_primary->itemWorld());
    m_secondary->bindSharedDisplayPipeline(&m_primary->hostDisplayPipeline());

    m_secondary->installEventFilter(this);
    m_splitter->addWidget(m_secondary);
}

void DualImageShell::destroySecondary()
{
    if (!m_secondary) {
        return;
    }
    if (m_active == m_secondary) {
        m_active = m_primary;
        m_primary->hostDisplayPipeline().setActiveHost(m_primary);
        emit activeViewChanged(m_primary);
    }
    m_secondary->removeEventFilter(this);
    m_secondary->deleteLater();
    m_secondary = nullptr;
    m_secondarySessionId = kInvalidSessionImageId;
    m_secondaryPath.clear();
}

void DualImageShell::setDualEnabled(bool on, SessionDocument *sessionDoc, SessionSeedBook *seedBook)
{
    ASSERT_GUI_THREAD();
    if (on == m_dual) {
        return;
    }
    m_dual = on;
    if (on) {
        ensureSecondary(sessionDoc, seedBook);
        if (m_secondary) {
            m_secondary->show();
            const int w = qMax(200, width());
            m_splitter->setSizes({w / 2, w / 2});
        }
    } else {
        if (m_secondary) {
            m_secondary->hide();
            destroySecondary();
        }
        m_active = m_primary;
        m_primary->hostDisplayPipeline().setActiveHost(m_primary);
    }
    emit dualEnabledChanged(m_dual);
}

void DualImageShell::openOnSecondary(const QString &path, SessionImageId sid)
{
    ASSERT_GUI_THREAD();
    if (!m_dual || !m_secondary || path.isEmpty()) {
        return;
    }

    noteFocus(m_secondary);

    m_secondary->hostImage().setClassicPath(path);
    m_secondary->setCurrentSessionId(sid);
    m_secondarySessionId = sid;
    m_secondaryPath = path;

    if (!m_secondary->isImageMode()) {
        m_secondary->hostImage().enter();
    } else {
        m_secondary->hostDisplayPipeline().loadImage(path);
    }

    emit secondarySessionChanged(sid, path);
}

bool DualImageShell::navigateSecondary(int delta, const QStringList &paths,
                                       const QVector<SessionImageId> &ids)
{
    ASSERT_GUI_THREAD();
    if (!m_dual || !m_secondary || paths.isEmpty() || delta == 0) {
        return false;
    }
    const int n = paths.size();
    int idx = -1;
    if (m_secondarySessionId != kInvalidSessionImageId && ids.size() == n) {
        for (int i = 0; i < n; ++i) {
            if (ids.at(i) == m_secondarySessionId) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0 && !m_secondaryPath.isEmpty()) {
        idx = paths.indexOf(m_secondaryPath);
    }
    if (idx < 0) {
        idx = 0;
    }
    int next = idx + delta;
    while (next < 0) {
        next += n;
    }
    while (next >= n) {
        next -= n;
    }
    const QString path = paths.at(next);
    const SessionImageId sid =
        (ids.size() == n) ? ids.at(next) : kInvalidSessionImageId;
    openOnSecondary(path, sid);
    return true;
}

void DualImageShell::noteFocus(ImageView *view)
{
    if (!view || (view != m_primary && view != m_secondary)) {
        return;
    }
    if (m_active == view) {
        return;
    }
    m_active = view;
    view->hostDisplayPipeline().setActiveHost(view);
    emit activeViewChanged(view);
}

bool DualImageShell::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        if (auto *iv = qobject_cast<ImageView *>(watched)) {
            noteFocus(iv);
        }
    }
    return QWidget::eventFilter(watched, event);
}
