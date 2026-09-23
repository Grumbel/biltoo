# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

# Version string (About / QApplication::applicationVersion):
#   Packaging (nix build): -DPROJECT_VERSION_FULL=0.1.0-dev.N+gHASH from flake.nix
#   Dev (nix develop / biltoo-configure): VERSION + git, same scheme — NOT git-describe.
#     flake: "${versionBase}.${revCount}+g${shortRev or dirtyShortRev or "dirty"}"
#     cmake: "${VERSION}.${rev-list --count}+g$(rev-parse --short=7)"
#
# cmake --build re-runs configure when VERSION or .git/HEAD (or index) changes
# via CMAKE_CONFIGURE_DEPENDS, so the baked version tracks commits without a
# manual biltoo-configure.
if(DEFINED PROJECT_VERSION_FULL AND NOT PROJECT_VERSION_FULL STREQUAL "")
  # Packaging or explicit -D: keep as-is (already Nix-shaped).
  string(STRIP "${PROJECT_VERSION_FULL}" PROJECT_VERSION_FULL)
else()
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_FULL LIMIT_COUNT 1)
  string(STRIP "${PROJECT_VERSION_FULL}" PROJECT_VERSION_FULL)

  if(PROJECT_VERSION_FULL MATCHES "-dev")
    find_package(Git QUIET)
    set(_biltoo_git_dir "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    if(Git_FOUND AND EXISTS "${_biltoo_git_dir}")
      # Worktree: .git is a file "gitdir: …"
      if(NOT IS_DIRECTORY "${_biltoo_git_dir}")
        file(READ "${_biltoo_git_dir}" _biltoo_gitfile)
        string(REGEX REPLACE "^gitdir:[ 	]*([^

]+).*" "\1" _biltoo_git_dir
               "${_biltoo_gitfile}")
        if(NOT IS_ABSOLUTE "${_biltoo_git_dir}")
          set(_biltoo_git_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_biltoo_git_dir}")
        endif()
      endif()

      execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-list --count HEAD
        OUTPUT_VARIABLE _biltoo_git_count
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _biltoo_git_count_rc
      )
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=7 HEAD
        OUTPUT_VARIABLE _biltoo_git_rev
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _biltoo_git_rev_rc
      )
      if(_biltoo_git_count_rc EQUAL 0 AND _biltoo_git_rev_rc EQUAL 0
         AND NOT _biltoo_git_count STREQUAL "" AND NOT _biltoo_git_rev STREQUAL "")
        # Match flake.nix: no git-describe, no separate -dirty suffix.
        set(PROJECT_VERSION_FULL
            "${PROJECT_VERSION_FULL}.${_biltoo_git_count}+g${_biltoo_git_rev}")
        message(STATUS "Dev version from git: ${PROJECT_VERSION_FULL}")
      endif()

      # Reconfigure on cmake --build when the commit or VERSION file changes.
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
      if(EXISTS "${_biltoo_git_dir}/HEAD")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
          "${_biltoo_git_dir}/HEAD")
      endif()
      if(EXISTS "${_biltoo_git_dir}/index")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
          "${_biltoo_git_dir}/index")
      endif()
    endif()
  endif()
endif()

# project() needs a numeric version; strip -dev / +git / similar suffixes.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" _project_version_cmake "${PROJECT_VERSION_FULL}")
if(NOT _project_version_cmake)
  set(_project_version_cmake "0.0.0")
endif()

project(biltoo VERSION ${_project_version_cmake} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# thumtoo is required (Store index, tiles, soft ladder, archive/page URIs).
set(THUMTOO_SOURCE_DIR "" CACHE PATH "Path to thumtoo source tree (required)")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets Svg OpenGLWidgets PrintSupport)
find_package(PkgConfig)

# Application translation units (everything except main).
# Shared so an optional ImageView characterization harness can link the
# same objects without duplicating the list (docs/IMAGEVIEW_CHARACTERIZATION.md).
set(BILTOO_LIB_SOURCES
  src/shell/mainwindow.cpp
  src/shell/mainwindow_ui.cpp
  src/shell/mainwindow_gallery.cpp
  src/shell/mainwindow_session.cpp
  src/shell/mainwindow_session_remove.cpp
  src/shell/mainwindow_expand.cpp
  src/shell/mainwindow_sort.cpp
  src/shell/mainwindow_nav.cpp
  src/shell/mainwindow_slideshow.cpp
  src/shell/mainwindow_clipboard.cpp
  src/shell/mainwindow_project.cpp
  src/shell/mainwindow_print.cpp
  src/session/sessionexport.cpp
  src/session/sessionexport.h
  src/shell/mainwindow.h
  src/shell/mainwindow_includes.h
  src/shell/icons.cpp
  src/shell/icons.h
  src/biltoo_logging.cpp
  src/biltoo_logging.h
  src/gallery/gallerylayout.cpp
  src/gallery/gallerylayout.h
  src/imageview.cpp
  src/imageview_accessors.cpp
  src/imageview_size_book.cpp
  src/gallery/gallerycontroller.cpp
  src/slideshow/slideshowcontroller.cpp
  src/slideshow/slideshowcontroller.h
  src/crop/cropcontroller.cpp
  src/crop/cropcontroller.h
  src/attention/attentioncontroller.cpp
  src/attention/attentioncontroller.h
  src/hud/hudmodel.cpp
  src/hud/hudmodel.h
  src/display/displaypipelinecontroller.cpp
  src/display/displaypipelinecontroller_load.cpp
  src/display/displaypipelinecontroller_item.cpp
  src/display/displaypipeline_jobs.cpp
  src/display/displaypipeline_jobs.h
  src/display/displaypipelinecontroller.h
  src/gallery/gallerycontroller.h
  src/gallery/gallerysizeresolve.cpp
  src/gallery/gallerysizeresolve.h
  src/display/tileneighborprefetch.cpp
  src/display/tileneighborprefetch.h
  src/workspace/workspacecontroller.cpp
  src/workspace/workspacecontroller.h
  src/image/imagecontroller.cpp
  src/image/imagecontroller.h
  src/imageview_modes.cpp
  src/imageview_canvas.cpp
  src/imageview_canvas_focus.cpp
  src/imageview_canvas_place.cpp
  src/imageview_selection.cpp
  src/imageview_session_bind.cpp
  src/imageview_session_remove.cpp
  src/imageview_appearance.cpp
  src/imageview_color_grade.cpp
  src/imageview_crop_appearance.cpp
  src/imageview_appearance_commit.cpp
  src/imageview_rematerialize.cpp
  src/imageview_bake.cpp
  src/imageview_item_state.cpp
  src/crop/cropgeometry.cpp
  src/crop/cropgeometry.h
  src/crop/cropsession.cpp
  src/crop/cropsession.h
  src/crop/cropappearancecommand.cpp
  src/crop/cropappearancecommand.h
  src/crop/croppathraster.h
  src/crop/cropflash.h
  src/crop/cropdebug.h
  src/view/viewportupdatehold.h
  src/attention/attentionsession.h
  src/attention/attentiongeometry.cpp
  src/attention/attentiongeometry.h
  src/shell/centreprogress.h
  src/workspace/grouptransformsession.h
  src/workspace/grouptransformgeometry.cpp
  src/workspace/grouptransformgeometry.h
  src/workspace/pageguidesession.h
  src/workspace/pageguidegeometry.cpp
  src/workspace/pageguidegeometry.h
  src/item/iteminteractsession.h
  src/item/itemframegeometry.cpp
  src/item/itemframegeometry.h
  src/text/textlayergeometry.cpp
  src/text/textlayergeometry.h
  src/item/itemhandle.h
  src/item/itemhandlepolicy.cpp
  src/item/itemhandlepolicy.h
  src/workspace/stackgeometry.cpp
  src/workspace/stackgeometry.h
  src/hud/hudflash.h
  src/view/viewframing.h
  src/text/textlayersession.h
  src/slideshow/zoomregiongesture.h
  src/view/canvasbackground.h
  src/layoutprefs.h
  src/view/viewportchrome.h
  src/hud/hudappearance.h
  src/session/sessionchrome.h
  src/gallery/gallerydecodebook.h
  src/perfstats.h
  src/layoutdebounce.h
  src/gallery/gallerypackfit.h
  src/selectiongeometry.h
  src/color/coloradjustcommit.h
  src/view/viewtransform.h
  src/workspace/workspacenavgeometry.h
  src/toolpolicy.h
  src/hud/hudgeometry.h
  src/text/textsearchpolicy.h
  src/text/textsearchpolicy.cpp
  src/view/canvaspatterngeometry.h
  src/gallery/galleryrelayoutsuppress.h
  src/layoutapplyguard.h
  src/imagesizebook.h
  src/pathitemstatebook.h
  src/pendingitemappearancebook.h
  src/slideshow/slideshowtypes.h
  src/slideshow/slideshowclocks.h
  src/slideshow/slideshowclocks.cpp
  src/slideshow/slideshowphasepolicy.h
  src/slideshow/slideshowphasepolicy.cpp
  src/slideshow/slideshowatlaspolicy.h
  src/slideshow/slideshowatlaspolicy.cpp
  src/slideshow/slideshowmotiongeometry.h
  src/slideshow/slideshowmotiongeometry.cpp
  src/slideshow/zoomblurhelpers.h
  src/slideshow/zoomblurhelpers.cpp
  src/loadgeneration.h
  src/session/sessionloadgate.h
  src/session/sessionbindbook.h
  src/session/sessionpathorder.h
  src/session/packorderoverlay.h
  src/display/displayedgepolicy.h
  src/display/displayedgepolicy.cpp
  src/image/edgenavpolicy.h
  src/image/edgenavpolicy.cpp
  src/slideshow/motionscrollchrome.h
  src/display/lqipdisplaypolicy.h
  src/display/lqipdisplaypolicy.cpp
  src/display/tile_load_coordinator.cpp
  src/imageview_framing_image.cpp
  src/imageview_status.cpp
  src/imageview_transform_actions.cpp
  src/imageview_input.cpp
  src/imageview_input_events.cpp
  src/imageview_workspace_chrome.cpp
  src/imageview_paint.cpp
  src/imageview_paint_background.cpp
  src/imageview_text.cpp
  src/imageview_pageguide.cpp
  src/imageview_export.cpp
  src/imageview_group.cpp
  src/imageview.h
  src/imageview_types.h
  src/session/sessionappearance.cpp
  src/session/sessionseedbook.cpp
  src/contentxform.cpp
  src/session/sessionappearance.h
  src/session/sessionseedbook.h
  src/session/sessiondocument.cpp
  src/session/sessiondocument.h
  src/session/sessionopen.cpp
  src/session/sessionopen.h
  src/session/sessionsort.cpp
  src/session/sessionsort.h
  src/session/sessionexpand.cpp
  src/session/sessionexpand.h
  src/session/projectfile.cpp
  src/session/projectfile.h
  src/placementlinear.cpp
  src/placementlinear.h
  src/imageitem.cpp
  src/display/displayquality.cpp
  src/display/displaysurface.cpp
  src/display/displaysurface.h
  src/imageitem_interaction.cpp
  src/imageitem_tilelod.cpp
  src/imageitem.h
  src/imageloader.cpp
  src/imageloader.h
  src/display/imagecache.cpp
  src/display/imagecache.h
  src/display/pathrasterservice.cpp
  src/display/pathrasterservice.h
  src/display/rasterclimbsm.cpp
  src/display/rasterclimbsm.h
  src/gallery/gallerydecodesm.cpp
  src/gallery/gallerydecodesm.h
  src/host/thumtoocache.cpp
  src/host/thumtoocache.h
  src/host/thumtoo_process_memos.cpp
  src/host/thumtoo_process_memos.h
  src/host/thumtoo_size_probe.cpp
  src/archivepath.cpp
  src/archivepath.h
  src/pagepath.cpp
  src/pagepath.h
  src/shell/thumbnailbar.cpp
  src/shell/thumbnailbar.h
  src/shell/preferencesdialog.cpp
  src/shell/preferencesdialog.h
  src/slideshow/slideshowsettingsdialog.cpp
  src/slideshow/slideshowsettingsdialog.h
  src/session/sessionreorderdialog.cpp
  src/session/sessionreorderdialog.h
  src/shell/epublayoutdialog.cpp
  src/shell/epublayoutdialog.h
  src/workspace/workspacebackgrounddialog.cpp
  src/workspace/workspacebackgrounddialog.h
  src/shell/metadatapanel.cpp
  src/shell/metadatapanel.h
  src/shell/adjustmentspanel.cpp
  src/shell/adjustmentspanel.h
  src/color/coloradjust.cpp
  src/color/coloradjust.h
  src/shell/layoutpanel.cpp
  src/shell/tocpanel.cpp
  src/shell/tocpanel.h
  src/shell/helppanel.cpp
  src/shell/helppanel.h
  src/shell/keyboardshortcutsdialog.cpp
  src/shell/keyboardshortcutsdialog.h
  src/shell/layoutpanel.h
  src/shell/defaultapps.cpp
  src/shell/defaultapps.h
  src/tilelod/lod_planner.cpp
  src/tilelod/draw_plan.cpp
  src/tilelod/tile_memory_cache.cpp
  src/tilelod/tile_session.cpp
  src/tilelod/tile_painter.cpp
  src/tilelod/thumtoo_tile_source.cpp
  src/tilelod/tile_lod_controller.cpp
  src/tilelod/tile_lod_registry.cpp
  src/shell/icons.qrc
)

# Application core as a static library so optional tests (ImageView
# characterization) can link the same objects + deps without duplicating lists.
add_library(biltoo_lib STATIC ${BILTOO_LIB_SOURCES})
target_include_directories(biltoo_lib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

add_executable(biltoo
  src/main.cpp
)
target_link_libraries(biltoo PRIVATE biltoo_lib)

# Stricter diagnostics (do not promote to errors by default).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(biltoo_lib PRIVATE
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
    target_compile_options(biltoo_lib PRIVATE
      -Wduplicated-cond
      -Wduplicated-branches
      -Wlogical-op
    )
  endif()
elseif(MSVC)
  target_compile_options(biltoo_lib PRIVATE /W4)
endif()

option(BILTOO_WERROR "Treat compiler warnings as errors" OFF)
if(BILTOO_WERROR)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(biltoo_lib PRIVATE -Werror)
  elseif(MSVC)
    target_compile_options(biltoo_lib PRIVATE /WX)
  endif()
endif()

# --- configure-time compile summary ---
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(_biltoo_build_type "(unset — multi-config or default)")
else()
  set(_biltoo_build_type "${CMAKE_BUILD_TYPE}")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  set(_biltoo_warn_flags "-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wformat=2 -Wundef -Wnull-dereference -Wdouble-promotion -Wmisleading-indentation")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND _biltoo_warn_flags " -Wduplicated-cond -Wduplicated-branches -Wlogical-op")
  endif()
  if(BILTOO_WERROR)
    string(APPEND _biltoo_warn_flags " -Werror")
  endif()
elseif(MSVC)
  set(_biltoo_warn_flags "/W4")
  if(BILTOO_WERROR)
    string(APPEND _biltoo_warn_flags " /WX")
  endif()
else()
  set(_biltoo_warn_flags "(compiler-default)")
endif()
message(STATUS "Biltoo compile mode: CMAKE_BUILD_TYPE=${_biltoo_build_type}")
message(STATUS "Biltoo C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Biltoo compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Biltoo warning flags: ${_biltoo_warn_flags}")
message(STATUS "Biltoo Werror: ${BILTOO_WERROR}")
# Typical flags by build type (generator still applies CMAKE_CXX_FLAGS_*):
#   Debug          → -g -O0 (asserts on, including ASSERT_NOT_GUI_THREAD)
#   RelWithDebInfo → -g -O2 (packaged nix build default)
#   Release        → -O3 / -DNDEBUG (Q_ASSERT often compiled out)
if(_biltoo_build_type STREQUAL "Debug")
  message(STATUS "Biltoo optim/debug: Debug (-g -O0 typical; Q_ASSERT active)")
elseif(_biltoo_build_type STREQUAL "RelWithDebInfo")
  message(STATUS "Biltoo optim/debug: RelWithDebInfo (-g -O2 typical)")
elseif(_biltoo_build_type STREQUAL "Release")
  message(STATUS "Biltoo optim/debug: Release (optimised; asserts may be stripped)")
elseif(_biltoo_build_type STREQUAL "MinSizeRel")
  message(STATUS "Biltoo optim/debug: MinSizeRel")
else()
  message(STATUS "Biltoo optim/debug: see CMAKE_CXX_FLAGS / multi-config generator")
endif()

target_link_libraries(biltoo_lib PUBLIC
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
set(BILTOO_FEATURE_THUMTOO_UNARR 0)
set(BILTOO_FEATURE_THUMTOO_MUPDF 0)
set(BILTOO_FEATURE_THUMTOO_DJVU 0)
set(BILTOO_FEATURE_THUMTOO_CURL 0)

if(PkgConfig_FOUND)
  # libvips — host ImageLoader extras (HEIF/AVIF buffer path, autorot, attention
  # crop). Not the Store decode path (that is thumtoo). Soft optional until
  # those features live entirely behind thumtoo.
  pkg_check_modules(VIPS IMPORTED_TARGET vips)
  if(VIPS_FOUND)
    target_link_libraries(biltoo_lib PUBLIC PkgConfig::VIPS)
    target_compile_definitions(biltoo_lib PUBLIC BILTOO_HAVE_VIPS=1)
    set(BILTOO_FEATURE_VIPS 1)
  endif()

  # libexiv2 — Metadata panel Exif/IPTC/XMP. Host UI only; not thumtoo Store.
  pkg_check_modules(EXIV2 IMPORTED_TARGET exiv2)
  if(EXIV2_FOUND)
    target_link_libraries(biltoo_lib PUBLIC PkgConfig::EXIV2)
    target_compile_definitions(biltoo_lib PUBLIC BILTOO_HAVE_EXIV2=1)
    set(BILTOO_FEATURE_EXIV2 1)
  endif()

  # Archives (zip/tar/…): handled by thumtoo (libarchive is thumtoo’s dependency).

  # gio-unix — GDesktopAppInfo for “default application” Preferences (Linux)
  pkg_check_modules(GIOUNIX IMPORTED_TARGET gio-unix-2.0)
  if(GIOUNIX_FOUND)
    target_link_libraries(biltoo_lib PUBLIC PkgConfig::GIOUNIX)
    target_compile_definitions(biltoo_lib PUBLIC BILTOO_HAVE_GIO=1)
    set(BILTOO_FEATURE_GIO 1)
  endif()
else()
  message(STATUS "PkgConfig not found — all optional native libraries disabled")
endif()

# thumtoo — required Store index + tiles / soft / archive-page URIs
if(NOT THUMTOO_SOURCE_DIR OR NOT EXISTS "${THUMTOO_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "thumtoo is required. Set -DTHUMTOO_SOURCE_DIR=/path/to/thumtoo "
    "(flake sets this via THUMTOO_SOURCE_DIR / biltoo-configure).")
endif()
set(THUMTOO_BUILD_TESTS OFF CACHE BOOL "thumtoo tests" FORCE)
set(THUMTOO_BUILD_TOOLS OFF CACHE BOOL "thumtoo tools" FORCE)
# thumtoo CMake treats PROJECT_VERSION_FULL as an explicit packaging override.
# Parent biltoo already set it to *our* VERSION+git — clear it for the nested
# configure so thumtoo reads its own tree (thumtoo ≥312 also refuses parent
# PROJECT_VERSION_FULL when not top-level). Restore afterward for version.h.
set(_biltoo_saved_project_version_full "${PROJECT_VERSION_FULL}")
unset(PROJECT_VERSION_FULL)
add_subdirectory("${THUMTOO_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/thumtoo" EXCLUDE_FROM_ALL)
set(PROJECT_VERSION_FULL "${_biltoo_saved_project_version_full}")
unset(_biltoo_saved_project_version_full)
target_link_libraries(biltoo_lib PUBLIC thumtoo)
set(BILTOO_FEATURE_THUMTOO 1)
# Archive containers are opened only through thumtoo (no biltoo ArchiveReader).
set(BILTOO_FEATURE_ARCHIVE 1)
# Sub-features from thumtoo (PARENT_SCOPE after add_subdirectory).
if(DEFINED THUMTOO_HAVE_UNARR AND THUMTOO_HAVE_UNARR)
  set(BILTOO_FEATURE_THUMTOO_UNARR 1)
endif()
if(DEFINED THUMTOO_HAVE_MUPDF AND THUMTOO_HAVE_MUPDF)
  set(BILTOO_FEATURE_THUMTOO_MUPDF 1)
endif()
if(DEFINED THUMTOO_HAVE_DJVU AND THUMTOO_HAVE_DJVU)
  set(BILTOO_FEATURE_THUMTOO_DJVU 1)
endif()
if(DEFINED THUMTOO_HAVE_CURL AND THUMTOO_HAVE_CURL)
  set(BILTOO_FEATURE_THUMTOO_CURL 1)
endif()
message(STATUS "thumtoo: required (${THUMTOO_SOURCE_DIR})")

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
biltoo_feature_status(BILTOO_FEATURE_THUMTOO_UNARR _st_unarr)
biltoo_feature_status(BILTOO_FEATURE_THUMTOO_MUPDF _st_mupdf)
biltoo_feature_status(BILTOO_FEATURE_THUMTOO_DJVU _st_djvu)
biltoo_feature_status(BILTOO_FEATURE_THUMTOO_CURL _st_curl)

message(STATUS "")
message(STATUS "Biltoo ${PROJECT_VERSION_FULL} — build summary")
message(STATUS "  Qt6 .............. required ≥ 6.9 (Core Gui Widgets Svg OpenGLWidgets PrintSupport)")
message(STATUS "  libvips .......... ${_st_vips}  (extra image codecs, autorot, attention Pan&Zoom)")
message(STATUS "  libexiv2 ......... ${_st_exiv2}  (Exif/IPTC/XMP metadata panel)")
message(STATUS "  archives ......... ${_st_archive}  (via thumtoo; zip/tar/7z/rar/…)")
message(STATUS "  gio-unix-2.0 ..... ${_st_gio}  (default-application Preferences)")
message(STATUS "  thumtoo .......... ${_st_thumtoo}  (durable size index + display ladder)")
message(STATUS "    libunarr ....... ${_st_unarr}  (solid RAR/CBR extract)")
message(STATUS "    MuPDF .......... ${_st_mupdf}  (PDF pages)")
message(STATUS "    DjVuLibre ...... ${_st_djvu}  (DjVu pages)")
message(STATUS "    libcurl ........ ${_st_curl}  (HTTP/S source fetch)")
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
install(FILES data/man/biltoo.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
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
    src/session/projectfile.cpp
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

  # Stage 3 pure: GalleryLayout axes swap + layoutSizeForNative (header-only).
  add_executable(biltoo-gallerylayout-test
    tests/gallerylayout_test.cpp
  )
  target_include_directories(biltoo-gallerylayout-test PRIVATE src)
  target_link_libraries(biltoo-gallerylayout-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME gallerylayout-pure COMMAND biltoo-gallerylayout-test)

  add_executable(biltoo-rasterclimbsm-test
    tests/rasterclimbsm_test.cpp
    src/display/rasterclimbsm.cpp
  )
  target_include_directories(biltoo-rasterclimbsm-test PRIVATE src)
  target_link_libraries(biltoo-rasterclimbsm-test PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME rasterclimbsm COMMAND biltoo-rasterclimbsm-test)

  add_executable(biltoo-gallerydecodesm-test
    tests/gallerydecodesm_test.cpp
    src/gallery/gallerydecodesm.cpp
  )
  target_include_directories(biltoo-gallerydecodesm-test PRIVATE src)
  target_link_libraries(biltoo-gallerydecodesm-test PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME gallerydecodesm COMMAND biltoo-gallerydecodesm-test)

  add_executable(biltoo-hudmodel-test
    tests/hudmodel_test.cpp
    src/hud/hudmodel.cpp
  )
  target_include_directories(biltoo-hudmodel-test PRIVATE src)
  target_link_libraries(biltoo-hudmodel-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME hudmodel COMMAND biltoo-hudmodel-test)

  add_executable(biltoo-sessiondocument-test
    tests/sessiondocument_test.cpp
    src/session/sessiondocument.cpp
    src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
    src/contentxform.cpp
    src/color/coloradjust.cpp
  )
  target_include_directories(biltoo-sessiondocument-test PRIVATE src)
  target_link_libraries(biltoo-sessiondocument-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME sessiondocument COMMAND biltoo-sessiondocument-test)

  add_executable(biltoo-sessionsort-test
    tests/sessionsort_test.cpp
    src/session/sessionsort.cpp
    src/pagepath.cpp
    src/archivepath.cpp
  )
  target_include_directories(biltoo-sessionsort-test PRIVATE src)
  target_link_libraries(biltoo-sessionsort-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME sessionsort COMMAND biltoo-sessionsort-test)

  add_executable(biltoo-sessionpathorder-test
    tests/sessionpathorder_test.cpp
  )
  target_include_directories(biltoo-sessionpathorder-test PRIVATE src)
  target_link_libraries(biltoo-sessionpathorder-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME sessionpathorder COMMAND biltoo-sessionpathorder-test)

  # Phase 7 Stage 0–2: ItemWorld facade + Placement characterization.
  # Links SessionAppearanceStore (ItemWorld dual-writes DTO + sparse tables).
  add_executable(biltoo-itemworld-test
    tests/itemworld_test.cpp
    src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
    src/contentxform.cpp
    src/color/coloradjust.cpp
  )
  target_include_directories(biltoo-itemworld-test PRIVATE src)
  target_link_libraries(biltoo-itemworld-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME itemworld COMMAND biltoo-itemworld-test)

  # Tier 4 characterization: full ImageView harness by default (links biltoo_lib).
  # OFF = pure scaffold only (low-RAM escape hatch). See docs/IMAGEVIEW_CHARACTERIZATION.md.
  option(BILTOO_IMAGEVIEW_CHARACTERIZATION
         "Link biltoo_lib into characterization test (offscreen ImageView)" ON)
  if(BILTOO_IMAGEVIEW_CHARACTERIZATION)
    message(STATUS "ImageView characterization: full harness (biltoo_lib)")
    add_executable(biltoo-imageview-characterization-test
      tests/imageview_characterization.cpp
    )
    target_link_libraries(biltoo-imageview-characterization-test PRIVATE
      biltoo_lib
      Qt6::Test
    )
    target_compile_definitions(biltoo-imageview-characterization-test PRIVATE
      BILTOO_HAVE_IMAGEVIEW_HARNESS=1
    )
  else()
    message(STATUS "ImageView characterization: pure scaffold only (-DBILTOO_IMAGEVIEW_CHARACTERIZATION=OFF)")
    add_executable(biltoo-imageview-characterization-test
      tests/imageview_characterization.cpp
      src/session/sessiondocument.cpp
      src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
      src/contentxform.cpp
      src/color/coloradjust.cpp
    )
    target_include_directories(biltoo-imageview-characterization-test PRIVATE src)
    target_link_libraries(biltoo-imageview-characterization-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  endif()
  add_test(NAME imageview-characterization COMMAND biltoo-imageview-characterization-test)

  # Dual model: SessionDocument vs SessionPathOrder independence (PATH_ORDER.md).
  add_executable(biltoo-pathorder-dual-model-test
    tests/pathorder_dual_model_test.cpp
    src/session/sessiondocument.cpp
    src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
    src/contentxform.cpp
    src/color/coloradjust.cpp
  )
  target_include_directories(biltoo-pathorder-dual-model-test PRIVATE src)
  target_link_libraries(biltoo-pathorder-dual-model-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME pathorder-dual-model COMMAND biltoo-pathorder-dual-model-test)

  add_executable(biltoo-packorderview-test
    tests/packorderview_test.cpp
    src/session/sessiondocument.cpp
    src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
    src/contentxform.cpp
    src/color/coloradjust.cpp
  )
  target_include_directories(biltoo-packorderview-test PRIVATE src)
  target_link_libraries(biltoo-packorderview-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME packorderview COMMAND biltoo-packorderview-test)

  add_executable(biltoo-packorderoverlay-test
    tests/packorderoverlay_test.cpp
    src/session/sessiondocument.cpp
    src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
    src/contentxform.cpp
    src/color/coloradjust.cpp
  )
  target_include_directories(biltoo-packorderoverlay-test PRIVATE src)
  target_link_libraries(biltoo-packorderoverlay-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME packorderoverlay COMMAND biltoo-packorderoverlay-test)

  add_executable(biltoo-session-gallery-crop-scenario-test
    tests/session_gallery_crop_scenario_test.cpp
    src/session/sessiondocument.cpp
    src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
    src/contentxform.cpp
    src/color/coloradjust.cpp
  )
  target_include_directories(biltoo-session-gallery-crop-scenario-test PRIVATE src)
  target_link_libraries(biltoo-session-gallery-crop-scenario-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME session-gallery-crop-scenario COMMAND biltoo-session-gallery-crop-scenario-test)

  add_executable(biltoo-sessionappearance-test
    tests/sessionappearance_test.cpp
    src/session/sessionappearance.cpp
    src/session/sessionseedbook.cpp
    src/session/sessiondocument.cpp
    src/contentxform.cpp
    src/color/coloradjust.cpp
  )
  target_include_directories(biltoo-sessionappearance-test PRIVATE src)
  target_link_libraries(biltoo-sessionappearance-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME sessionappearance COMMAND biltoo-sessionappearance-test)

  add_executable(biltoo-contentxform-test
    tests/contentxform_test.cpp
    src/contentxform.cpp
  )
  target_include_directories(biltoo-contentxform-test PRIVATE src)
  target_link_libraries(biltoo-contentxform-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME contentxform COMMAND biltoo-contentxform-test)

  # Paint-path contract: contentRect / layoutSize vs mapSourceRectToOriented
  # for every quarter-turn (and flips). Isolates "content rect does not rotate".
  add_executable(biltoo-content-orient-paint-contract-test
    tests/content_orient_paint_contract_test.cpp
    src/contentxform.cpp
  )
  target_include_directories(biltoo-content-orient-paint-contract-test PRIVATE src)
  target_link_libraries(biltoo-content-orient-paint-contract-test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME content_orient_paint_contract
    COMMAND biltoo-content-orient-paint-contract-test)

  add_executable(biltoo-displaysurface-test
    tests/displaysurface_test.cpp
    src/display/displaysurface.cpp
    src/contentxform.cpp
  )
  target_include_directories(biltoo-displaysurface-test PRIVATE src)
  target_link_libraries(biltoo-displaysurface-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
  add_test(NAME displaysurface COMMAND biltoo-displaysurface-test)

  # sessionappearance.cpp also needs ImageItem — crop geometry is covered in
  # contentxform_test (layoutSize + scaleCropRect formula). Do not link a partial
  # sessionappearance TU without imageitem (undefined refs at link time).
endif()

# tilelod core: pure C++ (no Qt)
add_executable(biltoo-tilelod-test
  tests/tilelod_test.cpp
  src/tilelod/lod_planner.cpp
  src/tilelod/draw_plan.cpp
  src/tilelod/tile_memory_cache.cpp
  src/tilelod/tile_session.cpp
)
target_include_directories(biltoo-tilelod-test PRIVATE src)
target_compile_features(biltoo-tilelod-test PRIVATE cxx_std_17)
add_test(NAME tilelod COMMAND biltoo-tilelod-test)
