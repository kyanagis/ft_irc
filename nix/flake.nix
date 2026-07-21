{
  description = "ft_irc ";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs:
        let
          llvm = pkgs.llvmPackages;
        in
        {
          default = pkgs.mkShell.override { stdenv = llvm.stdenv; } {
            packages = [
              # IRCクライアント
              pkgs.irssi
              pkgs.weechat

              pkgs.gnumake

              llvm.llvm

              pkgs.cppcheck
              pkgs.clang-tools

              pkgs.python3
            ]
            ++ pkgs.lib.optional pkgs.stdenv.isLinux pkgs.gcc;

            shellHook = ''
              echo "ft_irc dev shell — c++ = $(c++ --version | head -1)"
            '';
          };
        });
    };
}
