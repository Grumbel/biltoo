{ stdenv
, lib
, cmake
, pkg-config
, qt6
, wrapQtAppsHook
, vips
, exiv2
, glib
, libsysprof-capture
, fftw
, cfitsio
, libimagequant
, libarchive
, poppler  # thumtoo PDF pages (poppler-cpp.pc when BILTOO_WITH_THUMTOO)
, mupdf
, djvulibre
, kimageformats
, thumtooSrc ? null
, thumtooBuildInputs ? [ ]  # from thumtoo.lib.mkBuildInputs (libunarr, …)
, sqlite
, libjxl
  # Further vips Requires.private (and their .pc deps) — pkg-config noise only.
, cgif
, libexif
, libultrahdr
, libwebp
, pango
, fribidi
, libtiff
, librsvg
, dav1d
, matio
, hdf5
, lcms2
, openexr
, libraw
, openjpeg
, libhwy
, version ? "0.1.0-dev"
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "biltoo";
  inherit version;

  src = ./.;

  nativeBuildInputs = [
    cmake
    pkg-config
    wrapQtAppsHook
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtsvg
    qt6.qttools
    vips
    exiv2
    glib
    # glib Requires.private: sysprof-capture-4 — needed so pkg-config probes of
    # vips / gio-unix-2.0 do not spam "Package sysprof-capture-4 was not found".
    libsysprof-capture
    # vips Requires.private: fftw3 — same class of pkg-config noise without
    # the .pc on PKG_CONFIG_PATH (we do not link fftw ourselves).
    fftw
    # vips Requires.private: cfitsio — same pkg-config spam without the .pc.
    cfitsio
    # vips Requires.private: imagequant — same pkg-config spam without the .pc
    # (nixpkgs package name is libimagequant; module name is imagequant).
    libimagequant
    libarchive
    poppler
    mupdf
    djvulibre
    sqlite
    libjxl
    # Qt imageformat plugins: XCF (GIMP), KRA, ORA, extra RAW/PSD helpers, …
    kimageformats
    # More vips Requires.private (and transitive .pc names) so pkg_check_modules(vips)
    # does not spam "Package '…' was not found". We do not link these into biltoo.
    cgif
    libexif
    libultrahdr
    libwebp
    pango
    fribidi
    libtiff
    librsvg
    dav1d
    matio
    hdf5
    lcms2
    openexr
    libraw
    openjpeg
    libhwy
  ] ++ thumtooBuildInputs;

  # Keep symbols, strip into a separate "debug" output for gdb/coredumpctl.
  # Build with optimisations still on (not a full -O0 Debug build).
  cmakeBuildType = "RelWithDebInfo";
  separateDebugInfo = true;

  # ccacheStdenv: writable CCACHE_DIR before cmake probes the compiler.
  # Prefer host dirs mounted via extra-sandbox-paths for persistent nix-build hits.
  # Must *write* a probe file: [ -w ] / mkdir -p alone is not enough when tmp/
  # is owned by another uid (common host /var/cache/ccache layout).
  prePhases = [ "ccacheDirPhase" ];
  ccacheDirPhase = ''
    _biltoo_ccache_usable() {
      local d="$1"
      mkdir -p "$d/tmp" 2>/dev/null || return 1
      local probe="$d/tmp/.biltoo-write-test.$$"
      if ! ( : >"$probe" ) 2>/dev/null; then
        return 1
      fi
      rm -f "$probe" 2>/dev/null || true
      return 0
    }

    _chosen=""
    # Honour pre-set CCACHE_DIR only if actually writable inside the sandbox.
    if [ -n "''${CCACHE_DIR:-}" ] && _biltoo_ccache_usable "$CCACHE_DIR"; then
      _chosen="$CCACHE_DIR"
    else
      if [ -n "''${CCACHE_DIR:-}" ]; then
        echo "biltoo ccache: ignoring unwritable CCACHE_DIR=$CCACHE_DIR"
      fi
      for _cand in /var/cache/ccache /nix/var/cache/ccache; do
        if _biltoo_ccache_usable "$_cand"; then
          _chosen="$_cand"
          break
        fi
      done
    fi
    if [ -z "$_chosen" ]; then
      if [ -n "''${NIX_BUILD_TOP:-}" ]; then
        _chosen="$NIX_BUILD_TOP/.ccache"
      else
        _chosen="''${XDG_CACHE_HOME:-''${HOME:-/tmp}/.cache}/ccache-biltoo"
      fi
    fi
    export CCACHE_DIR="$_chosen"
    mkdir -p "$CCACHE_DIR/tmp"
    if ! _biltoo_ccache_usable "$CCACHE_DIR"; then
      # Last resort: always-writable build top (should not fail).
      export CCACHE_DIR="$NIX_BUILD_TOP/.ccache"
      mkdir -p "$CCACHE_DIR/tmp"
    fi
    _mode=shared-host
    case "$CCACHE_DIR" in
      "$NIX_BUILD_TOP"/*) _mode=ephemeral ;;
    esac
    echo "biltoo ccache: dir=$CCACHE_DIR mode=$_mode"
    if [ "$_mode" = ephemeral ]; then
      echo "biltoo ccache: no hits across nix builds — run: nix run .#ccache-check"
      echo "biltoo ccache: tip: sudo chown root:nixbld /var/cache/ccache && sudo chmod 2775 /var/cache/ccache"
      echo "biltoo ccache:      (or chmod 1777) and ensure extra-sandbox-paths lists the dir"
    fi
  '';

  # Nixpkgs Qt/KDE setup hooks inject many -DKDE_INSTALL_* and related cmake
  # cache vars (ECM-style install dirs). This project is plain CMake + Qt, not
  # KDEInstallDirs, so CMake would spam "Manually-specified variables were not
  # used". --no-warn-unused-cli silences that without pretending to consume them.
  # CMAKE_C_COMPILER is similarly unused (C++-only) but comes from stdenv.
  cmakeFlags = [
    "-Wno-unused-cli"
    "-DPROJECT_VERSION_FULL=${finalAttrs.version}"
  ] ++ lib.optionals (thumtooSrc != null) [
    "-DTHUMTOO_SOURCE_DIR=${thumtooSrc}"
    "-DBILTOO_WITH_THUMTOO=ON"
  ];

  # `nix flake check` / `nix build` with checks: run CMake tests
  # (projectfile-roundtrip unit tests + biltoo --help smoke).
  doCheck = true;
  preCheck = ''
    export QT_QPA_PLATFORM=offscreen
    # Nix builder HOME is /homeless-shelter (not writable). Point the XDG cache
    # root at a sandbox temp dir so thumtoo can create $XDG_CACHE_HOME/thumtoo.
    export XDG_CACHE_HOME="''${TMPDIR:-/tmp}/thumtoo-cache"
  '';

  meta = with lib; {
    description = "Classic Qt image viewer with Image, Gallery, and Workspace modes";
    homepage = "https://github.com/Grumbel/biltoo";
    license = licenses.gpl3Plus;
    maintainers = [ ];
    platforms = platforms.linux;
    mainProgram = "biltoo";
  };
})
