{ stdenv
, lib
, cmake
, pkg-config
, qt6
, wrapQtAppsHook
, vips
, exiv2
, opencv
, glib
, libsysprof-capture
, fftw
, cfitsio
, libimagequant
, libarchive
, poppler  # thumtoo PDF pages (poppler-cpp.pc)
, mupdf
, djvulibre
, kimageformats
, thumtooSrc ? null
, thumtooBuildInputs ? [ ]  # from thumtoo.lib.mkBuildInputs (libunarr, …)
, piperServer ? null  # text2sprech piper-server (bin/piper-server on PATH)
, piperModelsDir ? null  # TEXT2SPRECH_PIPER_MODELS (bundled voice directory)
, yunetModel ? null  # OpenCV Zoo YuNet ONNX (BILTOO_FACE_YUNET_MODEL)
, sfaceModel ? null  # OpenCV Zoo SFace ONNX (BILTOO_FACE_SFACE_MODEL)
, sqlite
, libjxl
, kddockwidgets
, leptonica  # tesseract.pc Requires: lept (pkg-config noise)
, systemd    # dbus-1.pc Requires.private: libsystemd (pkg-config noise)
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
, pipewire  # Qt Multimedia dlopen(libpipewire-0.3) for audio (TTS)
, version ? "0.1.0-dev"
  # When true (flake .#biltoo.withCcache): require ccacheStdenv + shared host
  # CCACHE_DIR. Default false so plain `nix build .#biltoo` needs no host cache.
, enableCcache ? false
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

  # TTS: piper-server on PATH so PiperServerManager finds it without --piper-socket.
  # MODELS points at the bundled voice when provided by the flake.
  # pipewire: Qt Multimedia resolves libpipewire-0.3 via dlopen (not a link
  # dependency). Without it on the wrapper LD_LIBRARY_PATH, startup logs
  #   qt.multimedia.symbolsresolver: Couldn't load pipewire-0.3 library
  # Speech still uses QAudioSink and may fall back; the message is noisy under Nix.
  qtWrapperArgs = [
    "--prefix" "LD_LIBRARY_PATH" ":" "${pipewire}/lib"
  ] ++ lib.optionals (piperServer != null) [
    "--prefix" "PATH" ":" "${piperServer}/bin"
  ] ++ lib.optionals (piperModelsDir != null) [
    "--set-default" "TEXT2SPRECH_PIPER_MODELS" piperModelsDir
  ] ++ lib.optionals (yunetModel != null) [
    "--set-default" "BILTOO_FACE_YUNET_MODEL" yunetModel
  ] ++ lib.optionals (sfaceModel != null) [
    "--set-default" "BILTOO_FACE_SFACE_MODEL" sfaceModel
  ];


  buildInputs = [
    qt6.qtbase
    qt6.qtsvg
    qt6.qttools
    qt6.qtmultimedia
    pipewire
    vips
    exiv2
    opencv
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
    kddockwidgets
    # tesseract.pc Requires: lept — silence pkg-config spam when probing OCR.
    leptonica
    # dbus-1.pc Requires.private: libsystemd — silence pkg-config when thumtoo probes dbus.
    systemd
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

  # Optional shared-host ccache (flake .#biltoo.withCcache / enableCcache=true).
  # Shared host CCACHE_DIR only (no ephemeral fallback). Must *write* a probe
  # under dir/tmp — [ -w ] alone misses wrong tmp/ ownership. Fail the build if
  # no shared path is usable; fix host perms rather than hide it.
  prePhases = lib.optionals enableCcache [ "ccacheDirPhase" ];
  ccacheDirPhase = lib.optionalString enableCcache ''
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
    if [ -n "''${CCACHE_DIR:-}" ] && _biltoo_ccache_usable "$CCACHE_DIR"; then
      _chosen="$CCACHE_DIR"
    else
      if [ -n "''${CCACHE_DIR:-}" ]; then
        echo "biltoo ccache: pre-set CCACHE_DIR=$CCACHE_DIR is not writable in the sandbox"
      fi
      for _cand in /var/cache/ccache /nix/var/cache/ccache; do
        if _biltoo_ccache_usable "$_cand"; then
          _chosen="$_cand"
          break
        fi
      done
    fi
    if [ -z "$_chosen" ]; then
      echo "biltoo ccache: FATAL — no writable shared cache (no ephemeral fallback)"
      echo "biltoo ccache: candidates: /var/cache/ccache /nix/var/cache/ccache (or CCACHE_DIR)"
      echo "biltoo ccache: fix host:"
      echo "  sudo mkdir -p /var/cache/ccache/tmp"
      echo "  sudo chown root:nixbld /var/cache/ccache"
      echo "  sudo chmod 2775 /var/cache/ccache   # or 1777"
      echo "  # nix.conf: extra-sandbox-paths = /var/cache/ccache"
      echo "  sudo systemctl restart nix-daemon"
      echo "biltoo ccache: diagnose: nix run .#ccache-check"
      echo "biltoo ccache: or build without ccache: nix build .#biltoo"
      exit 1
    fi
    export CCACHE_DIR="$_chosen"
    echo "biltoo ccache: dir=$CCACHE_DIR mode=shared-host"
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

  # Ship YuNet next to the binary so non-wrapper launches still find it via
  # $out/share/biltoo/models/… (yunetSearchPaths in facedetector_yunet.cpp).
  postInstall = lib.optionalString (yunetModel != null || sfaceModel != null) ''
    mkdir -p "$out/share/biltoo/models"
    ${lib.optionalString (yunetModel != null) ''
      cp -v ${yunetModel} \
        "$out/share/biltoo/models/face_detection_yunet_2023mar.onnx"
    ''}
    ${lib.optionalString (sfaceModel != null) ''
      cp -v ${sfaceModel} \
        "$out/share/biltoo/models/face_recognition_sface_2021dec.onnx"
    ''}
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
