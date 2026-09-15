{
  description = "A playground for microcontroller projects and experiments";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          commonTools = with pkgs; [
            gnumake
            cmake
            ninja
            pkg-config
            picocom
            openocd
            uv
          ];

          hostPythonDeps = with pkgs; [
            zlib
            openssl
            bzip2
            xz
            sqlite
            libffi
          ];

          nuttxTools = with pkgs; [
            kconfig-frontends
            gperf
            flex
            bison
            genromfs
            ncurses
            autoconf
            automake
            libtool
          ];

          pico2Tools = with pkgs; [
            gcc-arm-embedded
            picotool
          ];

          riscv32Pkgs = import nixpkgs {
            inherit system;
            crossSystem = {
              config = "riscv32-none-elf";
              libc = "newlib-nano";
              gcc = {
                arch = "rv32imac";
                abi = "ilp32";
              };
            };
          };

          esp32Tools = [
            riscv32Pkgs.buildPackages.gcc14
            riscv32Pkgs.buildPackages.binutils
          ];

          rustZigTools = with pkgs; [
            rustup
            zig
            probe-rs-tools
            espflash
          ];
        in
        {
          default = pkgs.mkShell {
            hardeningDisable = [ "all" ];

            packages = commonTools ++ hostPythonDeps ++ nuttxTools ++ pico2Tools
                       ++ esp32Tools ++ rustZigTools;

            shellHook = ''
              export PICO_SDK_PATH="$PWD/external/pico-sdk"
              export PLAYGROUND_ROOT="$PWD"

              # libffi runs autoreconf, and aclocal needs this to find
              # libtool's macros. Without it the build fails with
              # "Libtool library used but LIBTOOL is undefined".
              export ACLOCAL_PATH="${pkgs.libtool}/share/aclocal''${ACLOCAL_PATH:+:$ACLOCAL_PATH}"
            '';
          };
        }
      );
    };
}
