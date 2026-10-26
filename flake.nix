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
            git
            cmake
            ninja
            pkg-config
            picocom
            openocd
            uv
            python315
          ];

          hostPythonDeps = with pkgs; [
            zlib
            openssl
            bzip2
            xz
            sqlite
            libffi
            libxml2
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
            pkgs.cargo-generate
            pkgs.ldproxy
            pkgs.espup
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

              # Source Xtensa Rust toolchain environment if installed by espup.
              if [ -f "$HOME/export-esp.sh" ]; then
                . "$HOME/export-esp.sh"
              fi

              # Needed for clang/esp-clang to locate libxml2.so.2.
              export LD_LIBRARY_PATH="${pkgs.libxml2.out}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

              # Needed for aclocal to locate libtool macros during autoreconf.
              export ACLOCAL_PATH="${pkgs.libtool}/share/aclocal''${ACLOCAL_PATH:+:$ACLOCAL_PATH}"
            '';
          };
        }
      );
    };
}
