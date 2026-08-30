{
  description = "QImgView - Classic Qt image viewer with workspace semantics";

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
    in
    {
      packages.${system}.default = pkgs.qt6Packages.callPackage ./default.nix {
        inherit version;
      };

      apps.${system}.default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/qimgview";
      };

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ self.packages.${system}.default ];
        packages = with pkgs; [
          cmake
          gdb
          qt6.qttools
        ];
      };
    };
}
