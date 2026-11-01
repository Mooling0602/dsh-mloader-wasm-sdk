{
  description = "Development environment for the dsh-mloader WebAssembly SDK";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            name = "dsh-mloader-wasm-sdk";
            packages = with pkgs; [
              cargo
              clippy
              go
              lld
              nodejs
              rustc
              rustfmt
              wabt
              zig
            ];
            shellHook = ''
              echo "dsh-mloader-wasm-sdk development shell"
              echo "  rustc: $(rustc --version)"
              echo "  cargo: $(cargo --version)"
              echo "  go:    $(go version)"
              echo "  zig:   $(zig version)"
              echo "  node:  $(node --version)"
            '';
          };
        }
      );

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt);
    };
}
