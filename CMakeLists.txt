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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)
find_package(PkgConfig)

add_executable(qimgview
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow_ui.cpp
  src/mainwindow_gallery.cpp
  src/mainwindow_session.cpp
  src/mainwindow_print.cpp
  src/mainwindow.h
  src/mainwindow_includes.h
  src/icons.cpp
  src/icons.h
  src/gallerylayout.cpp
  src/gallerylayout.h
  src/imageview.cpp
  src/imageview_layout.cpp
  src/imageview_gallery.cpp
  src/gallerycontroller.cpp
  src/gallerycontroller.h
  src/workspacecontroller.cpp
  src/workspacecontroller.h
  src/imagecontroller.cpp
  src/imagecontroller.h
  src/imageview_workspace.cpp
  src/imageview_modes.cpp
  src/imageview_pack.cpp
  src/imageview_canvas.cpp
  src/imageview_crop.cpp
  src/imageview_load.cpp
  src/imageview_view.cpp
  src/imageview_transform.cpp
  src/imageview_input.cpp
  src/imageview_paint.cpp
  src/imageview_group.cpp
  src/imageview.h
  src/imageview_types.h
  src/sessionappearance.cpp
  src/sessionappearance.h
  src/sessiondocument.cpp
  src/sessiondocument.h
  src/projectfile.cpp
  src/projectfile.h
  src/imageitem.cpp
  src/imageitem_interaction.cpp
  src/imageitem.h
  src/imageloader.cpp
  src/imageloader.h
  src/archivepath.cpp
  src/archivepath.h
  src/archivereader.cpp
  src/archivereader.h
  src/thumbnailbar.cpp
  src/thumbnailbar.h
  src/preferencesdialog.cpp
  src/preferencesdialog.h
  src/workspacebackgrounddialog.cpp
  src/workspacebackgrounddialog.h
  src/metadatapanel.cpp
  src/metadatapanel.h
  src/adjustmentspanel.cpp
  src/adjustmentspanel.h
  src/coloradjust.cpp
  src/coloradjust.h
  src/layoutpanel.cpp
  src/layoutpanel.h
  src/defaultapps.cpp
  src/defaultapps.h
  src/icons.qrc
)

target_compile_definitions(qimgview PRIVATE
  QIMGVIEW_VERSION="${PROJECT_VERSION_FULL}"
)

# Stricter diagnostics (do not promote to errors by default).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(qimgview PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wformat=2
    -Wundef
    -Wnull-dereference
    -Wdouble-promotion
    -Wmisleading-indentation
  )
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(qimgview PRIVATE
      -Wduplicated-cond
      -Wduplicated-branches
      -Wlogical-op
    )
  endif()
elseif(MSVC)
  target_compile_options(qimgview PRIVATE /W4)
endif()

option(QIMGVIEW_WERROR "Treat compiler warnings as errors" OFF)
if(QIMGVIEW_WERROR)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(qimgview PRIVATE -Werror)
  elseif(MSVC)
    target_compile_options(qimgview PRIVATE /WX)
  endif()
endif()

target_link_libraries(qimgview PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::PrintSupport
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

  pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive)
  if(LIBARCHIVE_FOUND)
    target_link_libraries(qimgview PRIVATE PkgConfig::LIBARCHIVE)
    target_compile_definitions(qimgview PRIVATE QIMGVIEW_HAVE_ARCHIVE=1)
    message(STATUS "libarchive found — in-memory archive image loading enabled")
  else()
    message(STATUS "libarchive not found — archive paths disabled")
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
install(FILES data/qimgview.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
install(FILES data/icons/hicolor/scalable/apps/qimgview.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
# Optional shipped .qm files (none required to build)
file(GLOB QIMGVIEW_QM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.qm")
if(QIMGVIEW_QM_FILES)
  install(FILES ${QIMGVIEW_QM_FILES}
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/qimgview/translations)
endif()

message(STATUS "QImgView version: ${PROJECT_VERSION_FULL} (CMake PROJECT_VERSION=${PROJECT_VERSION})")

# AUDIT M24: minimal smoke test (CLI help) when the binary is built
enable_testing()
add_test(NAME qimgview-help
  COMMAND $<TARGET_FILE:qimgview> --help)
set_tests_properties(qimgview-help PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage:|Options:|qimgview")

# Project file load/save round-trip (no GUI)
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
  add_executable(qimgview-projectfile-test
    tests/projectfile_roundtrip.cpp
    src/projectfile.cpp
  )
  target_include_directories(qimgview-projectfile-test PRIVATE src)
  target_link_libraries(qimgview-projectfile-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME projectfile-roundtrip COMMAND qimgview-projectfile-test)
endif()
