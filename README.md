## Microcontroller Playground

[![Tests](https://img.shields.io/github/actions/workflow/status/habedi/microcontroller-playground/tests.yml?label=tests&style=flat&labelColor=282c34&logo=github&logoColor=white)](https://github.com/habedi/microcontroller-playground/actions/workflows/tests.yml)
[![Documentation](https://img.shields.io/badge/docs-latest-007ec6?style=flat&labelColor=282c34&logo=read-the-docs&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/docs)
[![License](https://img.shields.io/badge/license-Apache--2.0-007ec6?style=flat&labelColor=282c34&logo=open-source-initiative&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/LICENSE)
[![MCU](https://img.shields.io/badge/MCU-Pico%202%20WH%20%7C%20ESP32--P4%20%7C%20ESP32--S3-507ec6?style=flat&labelColor=282c34&logo=riscv&logoColor=white)](docs/)
[![RTOS](https://img.shields.io/badge/RTOS-NuttX%20%7C%20FreeRTOS-507ec6?style=flat&labelColor=282c34&logo=apache&logoColor=white)](https://nuttx.apache.org/)

---

This is a playground for my microcontroller projects and experiments.

### Supported Hardware

| # | Board / Module / MCU                | Specifications                                                                                                                                                | Local Notes                                |                                              Image                                              |
|---|-------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------|:-----------------------------------------------------------------------------------------------:|
| 1 | Raspberry Pi Pico 2 WH (RP2350)     | [Pico 2 Specs](https://www.raspberrypi.com/products/raspberry-pi-pico-2/), [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf) | [Pico 2 Notes](docs/raspberrypi-pico-2.md) |       <img src="docs/assets/images/pico2wh.png" alt="Raspberry Pi Pico 2 WH" width="150">       |
| 2 | Waveshare ESP32-P4-WIFI6 (ESP32-P4) | [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-P4-WIFI6), [Espressif ESP32-P4 Specs](https://www.espressif.com/en/products/socs/esp32-p4)              | [ESP32-P4 Notes](docs/esp32p4.md)          |  <img src="docs/assets/images/esp32-p4-wifi6.png" alt="Waveshare ESP32-P4-WIFI6" width="150">   |
| 3 | Waveshare ESP32-S3-Zero (ESP32-S3)  | [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Zero), [Espressif ESP32-S3 Specs](https://www.espressif.com/en/products/socs/esp32-s3), [Electrokit](https://www.electrokit.com/en/esp32-s3-dev-board-mini-8mb-psram-8mb-with-headers) | [ESP32-S3 Notes](docs/esp32s3.md)          | <img src="docs/assets/images/esp32-s3-zero-n8r8.png" alt="Waveshare ESP32-S3-Zero" width="150"> |

---

### Quickstart

#### 1. Clone the repository

```shell
git clone https://github.com/habedi/microcontroller-playground
cd microcontroller-playground
make submodules
```

#### 2. Enter the development environment

```shell
make shell
```

> [!IMPORTANT]
> The first `make shell` builds the RISC-V cross-compiler from source.
> Run `make shell-pin` afterward to protect it from Nix garbage collection.
> Espressif boards also need `make install` for esptool.

#### 3. Build and run on the Pico 2

```shell
make nuttx-configure BOARD=raspberrypi-pico-2:usbnsh
make nuttx-build
make flash-pico-uf2             # Hold the BOOTSEL button pressed down while plugging the board in
make console TTY=/dev/ttyACM0   # Picocom at 115200 baud; quit with C-a C-x
```

Press Enter twice after opening the console to reach the `nsh>` prompt.

<div align="center">
  <img alt="NuttShell" src="docs/assets/images/pico2w-nsh-1.jpeg" width="100%">
</div>

The `usbnsh` configuration provides a console over USB.
The `nsh` configuration routes to UART0 pins GP0 and GP1 via a USB-to-UART adapter.

Flashing needs BOOTSEL mode;
`make flash-pico-uf2` copies the UF2 to the exposed drive, while `make flash-pico` uses picotool (which needs `make setup-udev`).

#### 4. Build and run on the ESP32-P4

```shell
make nuttx-distclean            # Run this whenever you switch boards

# For the Waveshare board (silicon revision v1.3):
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/esp32p4-function-ev-board/nsh-rev1
# For silicon revisions v3.0 and newer, use:
# make nuttx-configure BOARD=esp32p4-function-ev-board:nsh

make nuttx-build
make nuttx-flash-esp PORT=/dev/ttyACM0
make console TTY=/dev/ttyACM0
```

> [!NOTE]
> The onboard WCH CH343 bridge links UART0 (GPIO 37 and GPIO 38) to `/dev/ttyACM0`.
> Silicon revisions below v3.0 need `configs/nuttx/esp32p4-function-ev-board/nsh-rev1` to bypass the NuttX revision check.
> Serial port access needs membership in the `dialout` group (can be added by running `sudo usermod -aG dialout $USER`).

#### 5. Build and run on the ESP32-S3

```shell
# ESP-IDF C firmware
make espidf-install ESPIDF_TARGET=esp32s3
make espidf-build ESPIDF_TARGET=esp32s3
make espidf-flash ESPIDF_PORT=/dev/ttyACM0
make espidf-monitor ESPIDF_PORT=/dev/ttyACM0

# Or idf-rs (Rust)
make idfrs-build IDFRS_SRC=experiments/rgb-cycle
make idfrs-flash IDFRS_PORT=/dev/ttyACM0
```

> [!NOTE]
> The USB-C connector on the ESP32-S3-Zero connects directly to the internal USB Serial/JTAG controller (`/dev/ttyACM0`, vendor `303a`).

---

### Rust and Zig Support

The Nix dev shell includes Rust with embedded targets, espflash, probe-rs, and Zig (0.17.0).
The following experiments are built without connecting a board:

```shell
make rust-build   # esp-rs UART heartbeat on the onboard ESP32-C6
make idfrs-build  # idf-rs RGB cycle on the ESP32-S3
make zig-build    # MicroZig GPIO blink on the Pico 2 WH
```

Rust experiments belong to the root Cargo workspace, which shares `Cargo.lock` and the release profile. Standalone toolchain packages such as
`rgb-cycle` use the Xtensa `esp` toolchain with `ldproxy`.
The root `build.zig` builds the Zig firmware, with MicroZig pinned in the root `build.zig.zon`.
All build commands run from the repository root.

See [Serial Heartbeat](experiments/serial-heartbeat/README.md), [RGB Cycle](experiments/rgb-cycle/README.md),
and [GPIO Blink](experiments/gpio-blink/README.md) for dependency pins, wiring, output paths, and flashing commands.
The Rust example replaces the C6's wireless firmware if flashed.
Current esp-rs P4 support does not cover this board's v1.3 silicon.

---

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for details on how to make a contribution.

### License

The content of this repository is licensed under the Apache License, Version 2.0 (see [LICENSE](LICENSE)).
