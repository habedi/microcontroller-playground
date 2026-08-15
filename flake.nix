{
  description = "A playground for microcontroller projects and experiments";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # Development happens on a Linux machine the boards plug into, on
      # either amd64 or arm64.
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          # Tools every experiment needs, whatever the board or the OS.
          commonTools = with pkgs; [
            gnumake
            cmake
            ninja
            pkg-config
            picocom
            openocd
            uv
          ];

          # The NuttX build system. A second OS (Zephyr, FreeRTOS, and so on)
          # gets its own list like this one.
          nuttxTools = with pkgs; [
            kconfig-frontends
            gperf
            flex
            bison
            genromfs
            ncurses
          ];

          # The Raspberry Pi Pico 2 (RP2350, Arm Cortex-M33).
          pico2Tools = with pkgs; [
            gcc-arm-embedded
            picotool
          ];

          # The ESP32-P4 and the ESP32-C6 (bare-metal RISC-V). NuttX expects
          # the riscv-none-elf- prefix. Either pass CROSSDEV=riscv32-none-elf-
          # to make, or build the exact toolchain from external/crosstool-ng.
          # esptool is not listed here; uv manages it through pyproject.toml.
          esp32Tools = with pkgs; [
            pkgsCross.riscv32-embedded.buildPackages.gcc
            pkgsCross.riscv32-embedded.buildPackages.binutils
          ];

          # Rust and Zig, for experiments beyond C. rustup adds the embedded
          # Rust targets: thumbv8m.main-none-eabihf for the RP2350,
          # riscv32imac-unknown-none-elf for the ESP32-C6, and
          # riscv32imafc-unknown-none-elf for the ESP32-P4. Zig cross compiles
          # to all three chips and also works as a C cross compiler.
          rustZigTools = with pkgs; [
            rustup
            zig
            probe-rs-tools
            espflash
          ];
        in
        {
          default = pkgs.mkShell {
            packages = commonTools ++ nuttxTools ++ pico2Tools ++ esp32Tools ++ rustZigTools;

            # The NuttX rp23xx port builds against the Pico SDK submodule.
            shellHook = ''
              export PICO_SDK_PATH="$PWD/external/pico-sdk"
            '';
          };
        }
      );
    };
}
