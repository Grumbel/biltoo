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
, version ? "0.1.0-dev"
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "qimgview";
  inherit version;

  src = ./.;

  nativeBuildInputs = [
    cmake
    pkg-config
    wrapQtAppsHook
  ];

  buildInputs = [
    qt6.qtbase
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
    # Qt imageformat plugins: XCF (GIMP), KRA, ORA, extra RAW/PSD helpers, …
    kimageformats
  ];

  # Keep symbols, strip into a separate "debug" output for gdb/coredumpctl.
  # Build with optimisations still on (not a full -O0 Debug build).
  cmakeBuildType = "RelWithDebInfo";
  separateDebugInfo = true;

  cmakeFlags = [
    "-DPROJECT_VERSION_FULL=${finalAttrs.version}"
  ];

  # `nix flake check` / `nix build` with checks: run CMake tests
  # (projectfile-roundtrip unit tests + qimgview --help smoke).
  doCheck = true;
  preCheck = ''
    export QT_QPA_PLATFORM=offscreen
  '';

  meta = with lib; {
    description = "Classic Qt image viewer with workspace semantics";
    homepage = "https://github.com/Grumbel/qimgview";
    license = licenses.gpl3Plus;
    maintainers = [ ];
    platforms = platforms.linux;
    mainProgram = "qimgview";
  };
})
