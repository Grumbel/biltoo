{ stdenv
, lib
, cmake
, pkg-config
, qt6
, wrapQtAppsHook
, vips
, exiv2
, glib
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
  ];

  # Keep symbols, strip into a separate "debug" output for gdb/coredumpctl.
  # Build with optimisations still on (not a full -O0 Debug build).
  cmakeBuildType = "RelWithDebInfo";
  separateDebugInfo = true;

  cmakeFlags = [
    "-DPROJECT_VERSION_FULL=${finalAttrs.version}"
  ];

  meta = with lib; {
    description = "Classic Qt image viewer with workspace semantics";
    homepage = "https://github.com/grumbel/qimgview";
    license = licenses.gpl3Plus;
    maintainers = [ ];
    platforms = platforms.linux;
    mainProgram = "qimgview";
  };
})
