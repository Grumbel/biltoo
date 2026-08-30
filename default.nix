{ stdenv
, lib
, cmake
, pkg-config
, qt6
, wrapQtAppsHook
, vips
}:

stdenv.mkDerivation rec {
  pname = "qimgview";
  version = "0.1.0";

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
  ];

  meta = with lib; {
    description = "Classic Qt image viewer with workspace semantics";
    homepage = "https://github.com/grumbel/qimgview"; # placeholder
    license = licenses.gpl3Plus;
    maintainers = [ ];
    platforms = platforms.linux;
    mainProgram = "qimgview";
  };
}
