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
, kimageformats
, thumtooSrc ? null
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
  ];

  # Keep symbols, strip into a separate "debug" output for gdb/coredumpctl.
  # Build with optimisations still on (not a full -O0 Debug build).
  cmakeBuildType = "RelWithDebInfo";
  separateDebugInfo = true;

  # Nixpkgs Qt/KDE setup hooks inject many -DKDE_INSTALL_* and related cmake
  # cache vars (ECM-style install dirs). This project is plain CMake + Qt, not
  # KDEInstallDirs, so CMake would spam "Manually-specified variables were not
  # used". --no-warn-unused-cli silences that without pretending to consume them.
  # CMAKE_C_COMPILER is similarly unused (C++-only) but comes from stdenv.
  cmakeFlags = [
    "--no-warn-unused-cli"
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
