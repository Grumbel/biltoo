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
  src/slideshowcontroller.cpp
  src/slideshowcontroller.h
  src/cropcontroller.cpp
  src/cropcontroller.h
  src/attentioncontroller.cpp
  src/attentioncontroller.h
  src/hudmodel.cpp
  src/hudmodel.h
  src/displaypipelinecontroller.cpp
  src/displaypipelinecontroller.h
  src/gallerycontroller.h
  src/gallerysizeresolve.cpp
  src/gallerysizeresolve.h
  src/tileneighborprefetch.cpp
  src/tileneighborprefetch.h
  src/workspacecontroller.cpp
  src/workspacecontroller.h
  src/imagecontroller.cpp
  src/imagecontroller.h
  src/imageview_workspace.cpp
  src/imageview_modes.cpp
  src/imageview_pack.cpp
  src/imageview_canvas.cpp
  src/imageview_crop.cpp
  src/imageview_crop_paint.cpp
  src/imageview_crop_input.cpp
  src/imageview_crop_apply.cpp
  src/imageview_crop_raster.cpp
  src/imageview_crop_enter.cpp
  src/imageview_appearance.cpp
  src/cropgeometry.cpp
  src/cropgeometry.h
  src/cropsession.cpp
  src/cropsession.h
  src/cropappearancecommand.cpp
  src/cropappearancecommand.h
  src/croppathraster.h
  src/cropflash.h
  src/cropdebug.h
  src/viewportupdatehold.h
  src/attentionsession.h
  src/attentiongeometry.cpp
  src/attentiongeometry.h
  src/centreprogress.h
  src/grouptransformsession.h
  src/grouptransformgeometry.cpp
  src/grouptransformgeometry.h
  src/pageguidesession.h
  src/pageguidegeometry.cpp
  src/pageguidegeometry.h
  src/iteminteractsession.h
  src/itemframegeometry.cpp
  src/itemframegeometry.h
  src/textlayergeometry.cpp
  src/textlayergeometry.h
  src/itemhandlepolicy.cpp
  src/itemhandlepolicy.h
  src/stackgeometry.cpp
  src/stackgeometry.h
  src/hudflash.h
  src/viewframing.h
  src/textlayersession.h
  src/zoomregiongesture.h
  src/canvasbackground.h
  src/layoutprefs.h
  src/viewportchrome.h
  src/hudappearance.h
  src/sessionchrome.h
  src/gallerysoftbook.h
  src/perfstats.h
  src/layoutdebounce.h
  src/gallerypackfit.h
  src/selectiongeometry.h
  src/coloradjustcommit.h
  src/viewtransform.h
  src/workspacenavgeometry.h
  src/toolpolicy.h
  src/hudgeometry.h
  src/textsearchpolicy.h
  src/textsearchpolicy.cpp
  src/canvaspatterngeometry.h
  src/galleryrelayoutsuppress.h
  src/layoutapplyguard.h
  src/imagesizebook.h
  src/pathitemstatebook.h
  src/pendingitemappearancebook.h
  src/slideshowtypes.h
  src/slideshowclocks.h
  src/slideshowclocks.cpp
  src/slideshowphasepolicy.h
  src/slideshowphasepolicy.cpp
  src/slideshowatlaspolicy.h
  src/slideshowatlaspolicy.cpp
  src/slideshowmotiongeometry.h
  src/slideshowmotiongeometry.cpp
  src/zoomblurhelpers.h
  src/zoomblurhelpers.cpp
  src/loadgeneration.h
  src/sessionloadgate.h
  src/sessionbindbook.h
  src/sessionpathorder.h
  src/displayedgepolicy.h
  src/displayedgepolicy.cpp
  src/edgenavpolicy.h
  src/edgenavpolicy.cpp
  src/motionscrollchrome.h
  src/softdisplaypolicy.h
  src/softdisplaypolicy.cpp
  src/imageview_attention.cpp
  src/imageview_load.cpp
  src/tile_load_coordinator.cpp
  src/imageview_view.cpp
  src/imageview_status.cpp
  src/imageview_transform.cpp
  src/imageview_input.cpp
  src/imageview_paint.cpp
  src/imageview_group.cpp
  src/imageview.h
  src/imageview_types.h
  src/sessionappearance.cpp
  src/contentxform.cpp
  src/sessionappearance.h
  src/sessiondocument.cpp
  src/sessiondocument.h
  src/sessionopen.cpp
  src/sessionopen.h
  src/projectfile.cpp
  src/projectfile.h
  src/placementlinear.cpp
  src/placementlinear.h
  src/imageitem.cpp
  src/displayquality.cpp
  src/displaysurface.cpp
  src/displaysurface.h
  src/imageitem_interaction.cpp
  src/imageitem.h
  src/imageloader.cpp
  src/imageloader.h
  src/imagecache.cpp
  src/imagecache.h
  src/pathrasterservice.cpp
  src/pathrasterservice.h
  src/rasterclimbsm.cpp
  src/rasterclimbsm.h
  src/gallerysoftsm.cpp
  src/gallerysoftsm.h
  src/thumtoocache.cpp
  src/thumtoocache.h
  src/thumtoo_process_memos.cpp
  src/thumtoo_process_memos.h
  src/thumtoo_size_probe.cpp
  src/archivepath.cpp
  src/archivepath.h
  src/pagepath.cpp
  src/pagepath.h
  src/thumbnailbar.cpp
  src/thumbnailbar.h
  src/preferencesdialog.cpp
  src/preferencesdialog.h
  src/slideshowsettingsdialog.cpp
  src/slideshowsettingsdialog.h
  src/epublayoutdialog.cpp
  src/epublayoutdialog.h
  src/workspacebackgrounddialog.cpp
  src/workspacebackgrounddialog.h
  src/metadatapanel.cpp
  src/metadatapanel.h
  src/adjustmentspanel.cpp
  src/adjustmentspanel.h
  src/coloradjust.cpp
  src/coloradjust.h
  src/layoutpanel.cpp
  src/tocpanel.cpp
  src/tocpanel.h
  src/helppanel.cpp
  src/helppanel.h
  src/keyboardshortcutsdialog.cpp
  src/keyboardshortcutsdialog.h
  src/layoutpanel.h
  src/defaultapps.cpp
  src/defaultapps.h
  src/tilelod/lod_planner.cpp
  src/tilelod/draw_plan.cpp
  src/tilelod/tile_memory_cache.cpp
  src/tilelod/tile_session.cpp
  src/tilelod/tile_painter.cpp
  src/tilelod/thumtoo_tile_source.cpp
  src/tilelod/tile_lod_controller.cpp
  src/tilelod/tile_lod_registry.cpp
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
    target_link_libraries(biltoo PRIVATE PkgConfig::VIPS)
    target_compile_definitions(biltoo PRIVATE BILTOO_HAVE_VIPS=1)
    set(BILTOO_FEATURE_VIPS 1)
  endif()

  # libexiv2 — Metadata panel Exif/IPTC/XMP. Host UI only; not thumtoo Store.
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
target_link_libraries(biltoo PRIVATE thumtoo)
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

  add_executable(biltoo-rasterclimbsm-test
    tests/rasterclimbsm_test.cpp
    src/rasterclimbsm.cpp
  )
  target_include_directories(biltoo-rasterclimbsm-test PRIVATE src)
  target_link_libraries(biltoo-rasterclimbsm-test PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME rasterclimbsm COMMAND biltoo-rasterclimbsm-test)

  add_executable(biltoo-gallerysoftsm-test
    tests/gallerysoftsm_test.cpp
    src/gallerysoftsm.cpp
  )
  target_include_directories(biltoo-gallerysoftsm-test PRIVATE src)
  target_link_libraries(biltoo-gallerysoftsm-test PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME gallerysoftsm COMMAND biltoo-gallerysoftsm-test)

  add_executable(biltoo-hudmodel-test
    tests/hudmodel_test.cpp
    src/hudmodel.cpp
    src/displayedgepolicy.cpp
  )
  target_include_directories(biltoo-hudmodel-test PRIVATE src)
  target_link_libraries(biltoo-hudmodel-test PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME hudmodel COMMAND biltoo-hudmodel-test)

  add_executable(biltoo-sessiondocument-test
    tests/sessiondocument_test.cpp
    src/sessiondocument.cpp
  )
  target_include_directories(biltoo-sessiondocument-test PRIVATE src)
  target_link_libraries(biltoo-sessiondocument-test PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME sessiondocument COMMAND biltoo-sessiondocument-test)

  add_executable(biltoo-sessionappearance-test
    tests/sessionappearance_test.cpp
    src/sessionappearance.cpp
    src/sessiondocument.cpp
    src/contentxform.cpp
    src/coloradjust.cpp
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

  add_executable(biltoo-displaysurface-test
    tests/displaysurface_test.cpp
    src/displaysurface.cpp
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
