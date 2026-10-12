{
  description = "A playground for microcontroller projects and experiments";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    zig-overlay = {
      url = "github:mitchellh/zig-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, zig-overlay, rust-overlay }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs {
            inherit system;
            overlays = [ (import rust-overlay) ];
          };

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

          rustToolchain = pkgs.rust-bin.stable.latest.default.override {
            targets = [
              "thumbv8m.main-none-eabihf"
              "riscv32imac-unknown-none-elf"
              "riscv32imafc-unknown-none-elf"
            ];
          };

          rustZigTools = [
            rustToolchain
            zig-overlay.packages.${system}."0.17.0"
            pkgs.probe-rs-tools
            pkgs.espflash
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

              # Needed for aclocal to locate libtool macros during autoreconf.
              export ACLOCAL_PATH="${pkgs.libtool}/share/aclocal''${ACLOCAL_PATH:+:$ACLOCAL_PATH}"
            '';
          };
        }
      );
    };
}
