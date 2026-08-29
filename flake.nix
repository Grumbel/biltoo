{
  description = "QImgView - Classic Qt image viewer with workspace semantics";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system}.default = pkgs.qt6Packages.callPackage ./default.nix { };

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
          # qtcreator  # optional, large
        ];
      };
    };
}
