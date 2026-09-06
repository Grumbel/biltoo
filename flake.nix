{
  description = "Biltoo — classic Qt image viewer (Image, Gallery, Workspace)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    thumtoo.url = "github:Grumbel/thumtoo";
  };

  outputs = { self, nixpkgs, thumtoo }:
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
        # Flake source of thumtoo (add_subdirectory in CMake; not a prebuilt package).
        thumtooSrc = thumtoo;
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
      #
      # Dev helpers are real PATH scripts (writeShellScriptBin), not shellHook
      # functions, so `nix develop -c biltoo-run` works (exec needs a binary).
      devShells.${system}.default =
        let
          # Recent nixpkgs dropped qtPluginPrefix on some Qt outputs; fall back
          # to the standard Qt 6 plugin layout under the lib output.
          qtPluginRoot = pkg:
            let
              libOut = pkgs.lib.getLib pkg;
              prefix = pkg.qtPluginPrefix or "lib/qt-6/plugins";
            in
            "${libOut}/${prefix}";
          qtPluginPath = pkgs.lib.concatStringsSep ":" [
            (qtPluginRoot pkgs.qt6.qtbase)
            (qtPluginRoot pkgs.qt6.qtsvg)
          ];

          # Shared preamble: require BILTOO_SOURCE (set by shellHook) and resolve
          # the out-of-tree build directory.
          biltooDevPreamble = ''
            set -euo pipefail
            if [ -z "''${BILTOO_SOURCE:-}" ]; then
              echo "$0: BILTOO_SOURCE is not set (enter the shell with: nix develop)" >&2
              exit 1
            fi
            BILTOO_BUILD_DIR="''${BILTOO_BUILD_DIR:-/tmp/biltoo-build}"
          '';

          biltooConfigure = pkgs.writeShellScriptBin "biltoo-configure" (
            biltooDevPreamble
            + ''
              cmake -S "$BILTOO_SOURCE" -B "$BILTOO_BUILD_DIR" -G Ninja \
                -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}" \
                -DBILTOO_WITH_THUMTOO=ON \
                -DTHUMTOO_SOURCE_DIR="''${THUMTOO_SOURCE_DIR:-${thumtoo}}"
            ''
          );

          biltooBuild = pkgs.writeShellScriptBin "biltoo-build" (
            biltooDevPreamble
            + ''
              if [ ! -f "$BILTOO_BUILD_DIR/build.ninja" ] && [ ! -f "$BILTOO_BUILD_DIR/Makefile" ]; then
                biltoo-configure || exit 1
              fi
              cmake --build "$BILTOO_BUILD_DIR" "$@"
            ''
          );

          biltooRun = pkgs.writeShellScriptBin "biltoo-run" (
            biltooDevPreamble
            + ''
              biltoo-build || exit 1
              if [ ! -x "$BILTOO_BUILD_DIR/biltoo" ]; then
                echo "biltoo-run: $BILTOO_BUILD_DIR/biltoo missing after build" >&2
                exit 1
              fi
              # Do not use qtWrapperArgs here — those are makeWrapper flags.
              # shellHook / inputsFrom already put Qt plugins on QT_PLUGIN_PATH.
              # No exec: keep an interactive shell after biltoo exits when typed
              # by hand; under `nix develop -c` the process ends either way.
              "$BILTOO_BUILD_DIR/biltoo" "$@"
            ''
          );

          # Debug build + gdb. Extra args are biltoo's (via gdb --args).
          biltooRunGdb = pkgs.writeShellScriptBin "biltoo-run-gdb" (
            biltooDevPreamble
            + ''
              biltoo-build || exit 1
              if [ ! -x "$BILTOO_BUILD_DIR/biltoo" ]; then
                echo "biltoo-run-gdb: $BILTOO_BUILD_DIR/biltoo missing after build" >&2
                exit 1
              fi
              if ! command -v gdb >/dev/null 2>&1; then
                echo "biltoo-run-gdb: gdb not found (should be in the nix develop shell)" >&2
                exit 1
              fi
              # Inherit QT_PLUGIN_PATH / XDG_DATA_DIRS from shellHook.
              # No exec: return to the interactive shell when gdb exits.
              gdb --args "$BILTOO_BUILD_DIR/biltoo" "$@"
            ''
          );
        in
        pkgs.mkShell {
          inputsFrom = [ biltoo ];
          packages = (with pkgs; [
            cmake
            ninja
            gdb
            qt6.qttools
          ]) ++ [
            biltooConfigure
            biltooBuild
            biltooRun
            biltooRunGdb
          ];
          CMAKE_BUILD_TYPE = "Debug";
          shellHook = ''
            # Source tree (stable even if someone cds away before biltoo-run).
            export BILTOO_SOURCE="$PWD"

            # Theme search: FreeDesktop wants <datadir>/icons/hicolor/...
            # Our layout is data/icons/hicolor/... so datadir = $BILTOO_SOURCE/data.
            export XDG_DATA_DIRS="$BILTOO_SOURCE/data''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"

            # Unwrapped out-of-tree biltoo does not get wrapQtAppsHook. Point Qt at
            # iconengines (svg) + imageformats from the same Qt the package uses.
            # (qtPluginPrefix is not always present on qtbase/qtsvg in current nixpkgs.)
            export QT_PLUGIN_PATH="${qtPluginPath}''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"

            # Out-of-tree build dir (override with BILTOO_BUILD_DIR=...).
            export BILTOO_BUILD_DIR="''${BILTOO_BUILD_DIR:-/tmp/biltoo-build}"
            export THUMTOO_SOURCE_DIR="''${THUMTOO_SOURCE_DIR:-${thumtoo}}"

            echo "biltoo dev shell (CMAKE_BUILD_TYPE=''${CMAKE_BUILD_TYPE:-Debug})"
            echo "  build dir: $BILTOO_BUILD_DIR"
            echo "  biltoo-configure   # cmake -S . -B \$BILTOO_BUILD_DIR -G Ninja (+ thumtoo)"
            echo "  THUMTOO_SOURCE_DIR=$THUMTOO_SOURCE_DIR"
            echo "  version: cmake reads VERSION + .git (About → full 0.1.0-dev.N+gHASH)"
            echo "  biltoo-build       # incremental cmake --build"
            echo "  biltoo-run [args]  # build + run out-of-tree binary"
            echo "  biltoo-run-gdb [args]  # build + gdb --args biltoo"
            echo "  nix build          # RelWithDebInfo package (wrapped)"
            echo "  nix build .#debug  # matching debug symbols"
            echo "  also: nix develop -c biltoo-run   # helpers are on PATH"
          '';
        };
    };
}
