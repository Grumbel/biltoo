{ stdenv
, lib
, cmake
, qt6
, wrapQtAppsHook
}:

stdenv.mkDerivation rec {
  pname = "qimgview";
  version = "0.1.0";

  src = ./.;

  nativeBuildInputs = [
    cmake
    wrapQtAppsHook
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qttools
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
