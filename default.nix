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

stdenv.mkDerivation rec {
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

  cmakeFlags = [
    "-DPROJECT_VERSION_FULL=${version}"
  ];

  meta = with lib; {
    description = "Classic Qt image viewer with workspace semantics";
    homepage = "https://github.com/grumbel/qimgview";
    license = licenses.gpl3Plus;
    maintainers = [ ];
    platforms = platforms.linux;
    mainProgram = "qimgview";
  };
}
