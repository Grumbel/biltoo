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

project(biltoo VERSION ${_project_version_cmake} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BILTOO_WITH_THUMTOO "Build against thumtoo (cache index + ladder)" ON)
set(THUMTOO_SOURCE_DIR "" CACHE PATH "Path to thumtoo source tree (flake input)")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg OpenGLWidgets PrintSupport)
find_package(PkgConfig)

add_executable(biltoo
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
  src/biltoo_logging.cpp
  src/biltoo_logging.h
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
  src/imageview_attention.cpp
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
  src/placementlinear.cpp
  src/placementlinear.h
  src/imageitem.cpp
  src/imageitem_interaction.cpp
  src/imageitem.h
  src/imageloader.cpp
  src/imageloader.h
  src/imagecache.cpp
  src/imagecache.h
  src/thumtoocache.cpp
  src/thumtoocache.h
  src/archivepath.cpp
  src/archivepath.h
  src/thumbnailbar.cpp
  src/thumbnailbar.h
  src/preferencesdialog.cpp
  src/preferencesdialog.h
  src/slideshowsettingsdialog.cpp
  src/slideshowsettingsdialog.h
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

target_include_directories(biltoo PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

# Stricter diagnostics (do not promote to errors by default).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(biltoo PRIVATE
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
    target_compile_options(biltoo PRIVATE
      -Wduplicated-cond
      -Wduplicated-branches
      -Wlogical-op
    )
  endif()
elseif(MSVC)
  target_compile_options(biltoo PRIVATE /W4)
endif()

option(BILTOO_WERROR "Treat compiler warnings as errors" OFF)
if(BILTOO_WERROR)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(biltoo PRIVATE -Werror)
  elseif(MSVC)
    target_compile_options(biltoo PRIVATE /WX)
  endif()
endif()

target_link_libraries(biltoo PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Svg
  Qt6::OpenGLWidgets
  Qt6::PrintSupport
)

# ---------------------------------------------------------------------------
# Optional dependencies (pkg-config). Each sets BILTOO_FEATURE_* for version.h
# and BILTOO_HAVE_* compile definitions used by #ifdef in the sources.
# ---------------------------------------------------------------------------
set(BILTOO_FEATURE_VIPS 0)
set(BILTOO_FEATURE_EXIV2 0)
set(BILTOO_FEATURE_ARCHIVE 0)
set(BILTOO_FEATURE_GIO 0)
set(BILTOO_FEATURE_THUMTOO 0)

if(PkgConfig_FOUND)
  # libvips — extra codecs (HEIF/AVIF/…), autorot, smartcrop attention for Pan&Zoom
  pkg_check_modules(VIPS IMPORTED_TARGET vips)
  if(VIPS_FOUND)
    target_link_libraries(biltoo PRIVATE PkgConfig::VIPS)
    target_compile_definitions(biltoo PRIVATE BILTOO_HAVE_VIPS=1)
    set(BILTOO_FEATURE_VIPS 1)
  endif()

  # libexiv2 — full Exif / IPTC / XMP in the Metadata panel
  pkg_check_modules(EXIV2 IMPORTED_TARGET exiv2)
  if(EXIV2_FOUND)
    target_link_libraries(biltoo PRIVATE PkgConfig::EXIV2)
    target_compile_definitions(biltoo PRIVATE BILTOO_HAVE_EXIV2=1)
    set(BILTOO_FEATURE_EXIV2 1)
  endif()

  # Archives (zip/tar/…): handled by thumtoo (libarchive is thumtoo’s dependency).

  # gio-unix — GDesktopAppInfo for “default application” Preferences (Linux)
  pkg_check_modules(GIOUNIX IMPORTED_TARGET gio-unix-2.0)
  if(GIOUNIX_FOUND)
    target_link_libraries(biltoo PRIVATE PkgConfig::GIOUNIX)
    target_compile_definitions(biltoo PRIVATE BILTOO_HAVE_GIO=1)
    set(BILTOO_FEATURE_GIO 1)
  endif()
else()
  message(STATUS "PkgConfig not found — all optional native libraries disabled")
endif()

# thumtoo — durable size index + display ladder (flake input / local source)
if(BILTOO_WITH_THUMTOO AND THUMTOO_SOURCE_DIR AND EXISTS "${THUMTOO_SOURCE_DIR}/CMakeLists.txt")
  set(THUMTOO_BUILD_TESTS OFF CACHE BOOL "thumtoo tests" FORCE)
  set(THUMTOO_BUILD_TOOLS OFF CACHE BOOL "thumtoo tools" FORCE)
  add_subdirectory("${THUMTOO_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/thumtoo" EXCLUDE_FROM_ALL)
  target_link_libraries(biltoo PRIVATE thumtoo)
  target_compile_definitions(biltoo PRIVATE BILTOO_HAVE_THUMTOO=1)
  set(BILTOO_FEATURE_THUMTOO 1)
  # Archive containers are opened only through thumtoo (no biltoo ArchiveReader).
  set(BILTOO_FEATURE_ARCHIVE 1)
  message(STATUS "thumtoo: enabled (${THUMTOO_SOURCE_DIR})")
elseif(BILTOO_WITH_THUMTOO)
  message(STATUS "thumtoo: requested but THUMTOO_SOURCE_DIR not set or missing CMakeLists.txt")
else()
  message(STATUS "thumtoo: disabled")
endif()

# Version + feature macros: only TUs that include version.h recompile on bump.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/version.h"
  @ONLY
)

# Configuration summary — human-readable enabled/missing (not 1/0).
macro(biltoo_feature_status _var _out)
  if(${_var})
    set(${_out} "enabled")
  else()
    set(${_out} "missing")
  endif()
endmacro()
biltoo_feature_status(BILTOO_FEATURE_VIPS _st_vips)
biltoo_feature_status(BILTOO_FEATURE_EXIV2 _st_exiv2)
biltoo_feature_status(BILTOO_FEATURE_ARCHIVE _st_archive)
biltoo_feature_status(BILTOO_FEATURE_GIO _st_gio)
biltoo_feature_status(BILTOO_FEATURE_THUMTOO _st_thumtoo)

message(STATUS "")
message(STATUS "Biltoo ${PROJECT_VERSION_FULL} — build summary")
message(STATUS "  Qt6 .............. required (Core Gui Widgets Svg OpenGLWidgets PrintSupport)")
message(STATUS "  libvips .......... ${_st_vips}  (extra image codecs, autorot, attention Pan&Zoom)")
message(STATUS "  libexiv2 ......... ${_st_exiv2}  (Exif/IPTC/XMP metadata panel)")
message(STATUS "  archives ......... ${_st_archive}  (via thumtoo; zip/tar/7z/rar/…)")
message(STATUS "  gio-unix-2.0 ..... ${_st_gio}  (default-application Preferences)")
message(STATUS "  thumtoo .......... ${_st_thumtoo}  (durable size index + display ladder)")
message(STATUS "  Runtime: install KImageFormats for XCF/KRA/ORA and other Qt plugins.")
message(STATUS "")

include(GNUInstallDirs)

install(TARGETS biltoo DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/biltoo.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES data/biltoo.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
install(FILES data/icons/hicolor/scalable/apps/biltoo.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
# Optional shipped .qm files (none required to build)
file(GLOB BILTOO_QM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.qm")
if(BILTOO_QM_FILES)
  install(FILES ${BILTOO_QM_FILES}
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/biltoo/translations)
endif()

message(STATUS "Biltoo version: ${PROJECT_VERSION_FULL} (CMake PROJECT_VERSION=${PROJECT_VERSION})")

# AUDIT M24: minimal smoke test (CLI help) when the binary is built
enable_testing()
add_test(NAME biltoo-help
  COMMAND $<TARGET_FILE:biltoo> --help)
set_tests_properties(biltoo-help PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage:|Options:|biltoo")

# Project file load/save round-trip (no GUI)
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
  add_executable(biltoo-projectfile-test
    tests/projectfile_roundtrip.cpp
    src/projectfile.cpp
  )
  target_include_directories(biltoo-projectfile-test PRIVATE src)
  target_link_libraries(biltoo-projectfile-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME projectfile-roundtrip COMMAND biltoo-projectfile-test)

  add_executable(biltoo-placementlinear-test
    tests/placementlinear_test.cpp
    src/placementlinear.cpp
  )
  target_include_directories(biltoo-placementlinear-test PRIVATE src)
  target_link_libraries(biltoo-placementlinear-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME placementlinear-roundtrip COMMAND biltoo-placementlinear-test)
endif()
