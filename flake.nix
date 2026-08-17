{
  description = "A playground for microcontroller projects and experiments";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # Development happens on a Linux machine the boards plug into, on either amd64 or arm64.
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

          # The ESP32-P4 and the ESP32-C6 (bare-metal RISC-V). The stock
          # nixpkgs riscv32-none-elf compiler defaults to rv32imafdc with the
          # ilp32d ABI and carries no multilib, so its libgcc cannot link the
          # soft-float rv32imac code that NuttX builds for either chip. This
          # toolchain sets the default architecture and ABI to the ones the
          # boards use. Enabling the ESP32-P4 FPU in NuttX would mean
          # rv32imafc with ilp32f here.
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

          # GCC 14, not 15, because GCC 15 defaults to C23, which removed
          # ATOMIC_VAR_INIT, and the Espressif HAL that NuttX vendors still
          # uses that macro. NuttX picks its command prefix from the board
          # configuration, and the Espressif boards ask for
          # riscv64-unknown-elf-, so a build has to pass
          # CROSSDEV=riscv32-none-elf- on the make command line to point it
          # here. esptool is not listed here; uv manages it through
          # pyproject.toml.
          esp32Tools = [
            riscv32Pkgs.buildPackages.gcc14
            riscv32Pkgs.buildPackages.binutils
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
            # PLAYGROUND_ROOT lets a NuttX configuration name a path in this
            # repository, such as the generated CYW43439 firmware blob, without
            # writing a machine specific path into a saved defconfig.
            shellHook = ''
              export PICO_SDK_PATH="$PWD/external/pico-sdk"
              export PLAYGROUND_ROOT="$PWD"
            '';
          };
        }
      );
    };
}
