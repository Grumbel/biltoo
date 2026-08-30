# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(qimgview VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig)

add_executable(qimgview
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/imageview.cpp
  src/imageview.h
  src/imageitem.cpp
  src/imageitem.h
  src/imageloader.cpp
  src/imageloader.h
  src/thumbnailbar.cpp
  src/thumbnailbar.h
  src/preferencesdialog.cpp
  src/preferencesdialog.h
  src/metadatapanel.cpp
  src/metadatapanel.h
  src/defaultapps.cpp
  src/defaultapps.h
  src/icons.qrc
)

target_link_libraries(qimgview PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)

# Optional libvips for formats Qt cannot decode
if(PkgConfig_FOUND)
  pkg_check_modules(VIPS IMPORTED_TARGET vips)
  if(VIPS_FOUND)
    target_link_libraries(qimgview PRIVATE PkgConfig::VIPS)
    target_compile_definitions(qimgview PRIVATE QIMGVIEW_HAVE_VIPS=1)
    message(STATUS "libvips found — broader image format support enabled")
  else()
    message(STATUS "libvips not found — using Qt image codecs only")
  endif()

  pkg_check_modules(EXIV2 IMPORTED_TARGET exiv2)
  if(EXIV2_FOUND)
    target_link_libraries(qimgview PRIVATE PkgConfig::EXIV2)
    target_compile_definitions(qimgview PRIVATE QIMGVIEW_HAVE_EXIV2=1)
    message(STATUS "libexiv2 found — full Exif/IPTC/XMP metadata enabled")
  else()
    message(STATUS "libexiv2 not found — using Qt image plugin metadata only")
  endif()

  pkg_check_modules(GIO IMPORTED_TARGET gio-2.0)
  if(GIO_FOUND)
    target_link_libraries(qimgview PRIVATE PkgConfig::GIO)
    target_compile_definitions(qimgview PRIVATE QIMGVIEW_HAVE_GIO=1)
    message(STATUS "GLib GIO found — default application associations enabled")
  else()
    message(STATUS "GLib GIO not found — default-app preferences disabled")
  endif()
endif()

include(GNUInstallDirs)

install(TARGETS qimgview DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/qimgview.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES data/icons/hicolor/scalable/apps/qimgview.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
