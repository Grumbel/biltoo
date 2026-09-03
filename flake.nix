{
  description = "QImgView — classic Qt image viewer (Image, Gallery, Workspace)";

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

      qimgview = pkgs.qt6Packages.callPackage ./default.nix {
        inherit version;
        kimageformats = pkgs.kdePackages.kimageformats;
      };
    in
    {
      packages.${system} = {
        default = qimgview;
        qimgview = qimgview;
        # Separate debug output from the package (ELF debuginfo under lib/debug).
        #   nix build .#debug
        #   gdb -ex "set debug-file-directory $(nix build --no-link --print-out-paths .#debug)/lib/debug" \
        #       $(nix build --no-link --print-out-paths)/bin/qimgview
        debug = qimgview.debug;
      };

      apps.${system}.default = {
        type = "app";
        program = "${qimgview}/bin/qimgview";
        meta = {
          description = "QImgView — classic Qt image viewer (Image, Gallery, Workspace)";
        };
      };

      # `nix flake check` builds the package and runs CMake tests
      # (projectfile-roundtrip + qimgview --help; see default.nix doCheck).
      checks.${system} = {
        qimgview = qimgview;
      };

      # Local cmake builds default to Debug. nix build still uses RelWithDebInfo
      # with separateDebugInfo (see default.nix).
      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ qimgview ];
        packages = with pkgs; [
          cmake
          gdb
          qt6.qttools
        ];
        CMAKE_BUILD_TYPE = "Debug";
        shellHook = ''
          echo "qimgview: CMAKE_BUILD_TYPE=Debug for in-tree cmake builds."
          echo "  nix build            → RelWithDebInfo binary (stripped)"
          echo "  nix build .#debug    → matching debug symbols"
        '';
      };
    };
}
