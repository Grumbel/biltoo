{
  description = "Biltoo — classic Qt image viewer (Image, Gallery, Workspace)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      versionBase = nixpkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
      gitRev = "${self.shortRev or self.dirtyShortRev or "dirty"}";
      isDev = nixpkgs.lib.strings.hasInfix "-dev" versionBase;
      version =
        if isDev then
          "${versionBase}.${toString (self.revCount or 0)}+g${gitRev}"
        else
          versionBase;

      biltoo = pkgs.qt6Packages.callPackage ./default.nix {
        inherit version;
        kimageformats = pkgs.kdePackages.kimageformats;
      };
    in
    {
      packages.${system} = {
        default = biltoo;
        biltoo = biltoo;
        # Separate debug output from the package (ELF debuginfo under lib/debug).
        #   nix build .#debug
        #   gdb -ex "set debug-file-directory $(nix build --no-link --print-out-paths .#debug)/lib/debug" \
        #       $(nix build --no-link --print-out-paths)/bin/biltoo
        debug = biltoo.debug;
      };

      apps.${system}.default = {
        type = "app";
        program = "${biltoo}/bin/biltoo";
        meta = {
          description = "Biltoo — classic Qt image viewer (Image, Gallery, Workspace)";
        };
      };

      # `nix flake check` builds the package and runs CMake tests
      # (projectfile-roundtrip + biltoo --help; see default.nix doCheck).
      checks.${system} = {
        biltoo = biltoo;
      };

      # Local cmake builds default to Debug. nix build still uses RelWithDebInfo
      # with separateDebugInfo (see default.nix).
      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ biltoo ];
        packages = with pkgs; [
          cmake
          ninja
          gdb
          qt6.qttools
        ];
        CMAKE_BUILD_TYPE = "Debug";
        shellHook = ''
          # Source tree (stable even if someone cds away before biltoo-run).
          export BILTOO_SOURCE="$PWD"

          # Theme search: FreeDesktop wants <datadir>/icons/hicolor/...
          # Our layout is data/icons/hicolor/... so datadir = $BILTOO_SOURCE/data.
          export XDG_DATA_DIRS="$BILTOO_SOURCE/data''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"

          # Out-of-tree build dir (override with BILTOO_BUILD_DIR=...).
          export BILTOO_BUILD_DIR="''${BILTOO_BUILD_DIR:-/tmp/biltoo-build}"

          biltoo-configure() {
            cmake -S "$BILTOO_SOURCE" -B "$BILTOO_BUILD_DIR" -G Ninja \
              -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}"
          }
          biltoo-build() {
            if [ ! -f "$BILTOO_BUILD_DIR/build.ninja" ] && [ ! -f "$BILTOO_BUILD_DIR/Makefile" ]; then
              biltoo-configure || return 1
            fi
            cmake --build "$BILTOO_BUILD_DIR" "$@"
          }
          biltoo-run() {
            biltoo-build || return 1
            if [ ! -x "$BILTOO_BUILD_DIR/biltoo" ]; then
              echo "biltoo-run: $BILTOO_BUILD_DIR/biltoo missing after build" >&2
              return 1
            fi
            # Do not use qtWrapperArgs here — those are makeWrapper flags.
            # inputsFrom already put Qt plugins on QT_PLUGIN_PATH.
            # No exec: keep the interactive shell after biltoo exits.
            "$BILTOO_BUILD_DIR/biltoo" "$@"
          }

          echo "biltoo dev shell (CMAKE_BUILD_TYPE=''${CMAKE_BUILD_TYPE:-Debug})"
          echo "  build dir: $BILTOO_BUILD_DIR"
          echo "  biltoo-configure   # cmake -S . -B \$BILTOO_BUILD_DIR -G Ninja"
          echo "  biltoo-build       # incremental cmake --build"
          echo "  biltoo-run [args]  # build + run out-of-tree binary"
          echo "  nix build          # RelWithDebInfo package (wrapped)"
          echo "  nix build .#debug  # matching debug symbols"
        '';
      };
    };
}
