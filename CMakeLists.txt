# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

# Source of truth: top-level VERSION (e.g. "0.1.0-dev"), or
# -DPROJECT_VERSION_FULL=... from packaging (Nix may append .<revCount>+g<rev>).
if(NOT DEFINED PROJECT_VERSION_FULL)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_FULL LIMIT_COUNT 1)
endif()

# project() needs a numeric version; strip -dev / +git / similar suffixes.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" _project_version_cmake "${PROJECT_VERSION_FULL}")
if(NOT _project_version_cmake)
  set(_project_version_cmake "0.0.0")
endif()

project(qimgview VERSION ${_project_version_cmake} LANGUAGES CXX)

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
  src/mainwindow_ui.cpp
  src/mainwindow_gallery.cpp
  src/mainwindow_session.cpp
  src/mainwindow.h
  src/icons.cpp
  src/icons.h
  src/gallerylayout.cpp
  src/gallerylayout.h
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

target_compile_definitions(qimgview PRIVATE
  QIMGVIEW_VERSION="${PROJECT_VERSION_FULL}"
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

  # gio-unix provides GDesktopAppInfo (gdesktopappinfo.h); pulls in gio-2.0
  pkg_check_modules(GIOUNIX IMPORTED_TARGET gio-unix-2.0)
  if(GIOUNIX_FOUND)
    target_link_libraries(qimgview PRIVATE PkgConfig::GIOUNIX)
    target_compile_definitions(qimgview PRIVATE QIMGVIEW_HAVE_GIO=1)
    message(STATUS "GLib GIO (unix) found — default application associations enabled")
  else()
    message(STATUS "gio-unix-2.0 not found — default-app preferences disabled")
  endif()
endif()

include(GNUInstallDirs)

install(TARGETS qimgview DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/qimgview.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES data/icons/hicolor/scalable/apps/qimgview.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)

message(STATUS "QImgView version: ${PROJECT_VERSION_FULL} (CMake PROJECT_VERSION=${PROJECT_VERSION})")
