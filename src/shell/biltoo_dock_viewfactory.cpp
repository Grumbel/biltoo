// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "biltoo_dock_viewfactory.h"

#include <kddockwidgets/qtwidgets/views/TitleBar.h>
#include <kddockwidgets/core/TitleBar.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QEvent>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QTimer>

namespace biltoo {
namespace {

bool controllerIsFilmstrip(KDDockWidgets::Core::TitleBar *controller)
{
    if (!controller) {
        return false;
    }
    const auto docks = controller->dockWidgets();
    for (KDDockWidgets::Core::DockWidget *dw : docks) {
        if (dw && dw->uniqueName() == QLatin1String(kFilmstripDockName)) {
            return true;
        }
    }
    return false;
}

/// Title bar that collapses to zero height once the filmstrip dock is attached.
/// KDDW creates the Group title bar before the dock widget is inserted, so the
/// factory cannot decide at construction time from dockWidgets() alone.
class FilmstripAwareTitleBar final : public KDDockWidgets::QtWidgets::TitleBar
{
public:
    explicit FilmstripAwareTitleBar(KDDockWidgets::Core::TitleBar *controller,
                                    KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::TitleBar(controller, parent)
    {
        // Re-check after the group receives its first dock widget.
        QTimer::singleShot(0, this, [this]() { syncFilmstripChrome(); });
    }

    QSize sizeHint() const override
    {
        if (m_collapsed) {
            return {0, 0};
        }
        return KDDockWidgets::QtWidgets::TitleBar::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        if (m_collapsed) {
            return {0, 0};
        }
        return KDDockWidgets::QtWidgets::TitleBar::minimumSizeHint();
    }

protected:
    bool event(QEvent *e) override
    {
        if (e && (e->type() == QEvent::Show || e->type() == QEvent::ParentChange
                  || e->type() == QEvent::ChildAdded)) {
            syncFilmstripChrome();
        }
        return KDDockWidgets::QtWidgets::TitleBar::event(e);
    }

private:
    void syncFilmstripChrome()
    {
        if (m_collapsed) {
            return;
        }
        if (!controllerIsFilmstrip(titleBar())) {
            return;
        }
        m_collapsed = true;
        setFixedHeight(0);
        setMaximumHeight(0);
        setMinimumHeight(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        updateGeometry();
        if (parentWidget()) {
            parentWidget()->updateGeometry();
        }
    }

    bool m_collapsed = false;
};

} // namespace

KDDockWidgets::Core::View *DockViewFactory::createTitleBar(
    KDDockWidgets::Core::TitleBar *controller,
    KDDockWidgets::Core::View *parent) const
{
    // Always use the aware bar: it is a no-op for non-filmstrip docks and
    // collapses only when ThumbnailDock is present in the group.
    return new FilmstripAwareTitleBar(controller, parent);
}

} // namespace biltoo

