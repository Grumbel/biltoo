// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_DOCK_VIEWFACTORY_H
#define BILTOO_DOCK_VIEWFACTORY_H

#include <kddockwidgets/qtwidgets/ViewFactory.h>

namespace biltoo {

/// Unique name of the filmstrip dock (MainWindow::m_thumbnailDock).
inline constexpr const char kFilmstripDockName[] = "ThumbnailDock";

/// ViewFactory: filmstrip gets a zero-height title bar; all other docks default.
class DockViewFactory final : public KDDockWidgets::QtWidgets::ViewFactory
{
    Q_OBJECT
public:
    DockViewFactory() = default;

    using KDDockWidgets::QtWidgets::ViewFactory::createTitleBar;

    KDDockWidgets::Core::View *createTitleBar(KDDockWidgets::Core::TitleBar *controller,
                                              KDDockWidgets::Core::View *parent) const override;
};

} // namespace biltoo

#endif
