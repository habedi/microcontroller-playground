## Microcontroller Playground

[![Tests](https://img.shields.io/github/actions/workflow/status/habedi/microcontroller-playground/tests.yml?label=tests&style=flat&labelColor=282c34&logo=github&logoColor=white)](https://github.com/habedi/microcontroller-playground/actions/workflows/tests.yml)
[![MCU](https://img.shields.io/badge/MCU-Pico%202%20W%20%7C%20ESP32--{P4,S3,C6}-507ec6?style=flat&labelColor=282c34&logo=riscv&logoColor=white)](docs/)
[![RTOS](https://img.shields.io/badge/RTOS-NuttX%20%7C%20FreeRTOS-507ec6?style=flat&labelColor=282c34&logo=apache&logoColor=white)](https://nuttx.apache.org/)
[![Documentation](https://img.shields.io/badge/docs-latest-007ec6?style=flat&labelColor=282c34&logo=read-the-docs&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/docs)
[![License](https://img.shields.io/badge/license-Apache--2.0-007ec6?style=flat&labelColor=282c34&logo=open-source-initiative&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/LICENSE)

---

This is a playground for my microcontroller projects and experiments.

### Supported Hardware

| # | Board / Module / MCU                | Specifications                                                                                                                                                                                                                                  | My Notes                                   |                                              Image                                              |
|---|-------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------|:-----------------------------------------------------------------------------------------------:|
| 1 | Raspberry Pi Pico 2 WH (RP2350)     | [Pico 2 Specs](https://www.raspberrypi.com/products/raspberry-pi-pico-2/), [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)                                                                                   | [Pico 2 Notes](docs/raspberrypi-pico-2.md) |       <img src="docs/assets/images/pico2wh.png" alt="Raspberry Pi Pico 2 WH" width="150">       |
| 2 | Waveshare ESP32-P4-WIFI6 (ESP32-P4) | [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-P4-WIFI6), [Espressif ESP32-P4 Specs](https://www.espressif.com/en/products/socs/esp32-p4)                                                                                                | [ESP32-P4 Notes](docs/esp32p4.md)          |  <img src="docs/assets/images/esp32-p4-wifi6.png" alt="Waveshare ESP32-P4-WIFI6" width="150">   |
| 3 | Waveshare ESP32-S3-Zero (ESP32-S3)  | [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Zero), [Espressif ESP32-S3 Specs](https://www.espressif.com/en/products/socs/esp32-s3), [Electrokit](https://www.electrokit.com/en/esp32-s3-dev-board-mini-8mb-psram-8mb-with-headers) | [ESP32-S3 Notes](docs/esp32s3.md)          | <img src="docs/assets/images/esp32-s3-zero-n8r8.png" alt="Waveshare ESP32-S3-Zero" width="150"> |
| 4 | ESP32-C6-DevKitC-1 (ESP32-C6)       | [Espressif ESP32-C6 Specs](https://www.espressif.com/en/products/socs/esp32-c6), [Electrokit](https://www.electrokit.com/en/esp32-c6-devkitc-utvecklingskort-8mb)                                                                               | [ESP32-C6 Notes](docs/esp32c6.md)          |  <img src="docs/assets/images/esp32-c6-devkitc-8mb.png" alt="ESP32-C6-DevKitC-1" width="150">   |

---

### Quickstart

To follow the steps below, you need to have Git and [Nix](https://nixos.org/download/) (with flakes enabled) installed on your machine.

#### 1. Clone the repository

```shell
git clone https://github.com/habedi/microcontroller-playground
cd microcontroller-playground
make submodules
```

#### 2. Set up the development environment

```shell
nix develop
```

> [!IMPORTANT]
> The first `make shell` builds the RISC-V cross-compiler from source.
> Run `make shell-pin` afterward to protect it from Nix garbage collection.
> Espressif boards also need `make install` for esptool.

> [!NOTE]
> Serial port access needs membership in the `dialout` group (can be added by running `sudo usermod -aG dialout $USER`).

#### 3. Build and run on the Pico 2

```shell
# Apache NuttX-based firmware
make nuttx-configure BOARD=raspberrypi-pico-2:usbnsh
make nuttx-build
make flash-pico-uf2             # Hold the BOOTSEL button pressed down while plugging the board in
make console TTY=/dev/ttyACM0   # Picocom at 115200 baud; quit with C-a C-x
```

Press Enter twice after opening the console to reach the `nsh>` prompt.

<div align="center">
  <img alt="NuttShell" src="docs/assets/images/pico2w-nsh-1.jpeg" width="100%">
</div>

#### 4. Build and run on the ESP32-P4

```shell
# Apache NuttX-based firmware
make nuttx-distclean            # Run this whenever you switch boards

make nuttx-configure BOARD=esp32p4-function-ev-board:nsh
# Required for the Waveshare board (silicon revision v1.3)
kconfig-tweak --file external/nuttx/.config \
  --enable CONFIG_ESP32P4_SELECTS_REV_LESS_V3 \
  --enable CONFIG_ESP32P4_REV_MIN_100
make -C external/nuttx olddefconfig

make nuttx-build
make nuttx-flash-esp PORT=/dev/ttyACM0
make console TTY=/dev/ttyACM0
```

Check out [experiments/espidf-sdcard](experiments/espidf-sdcard) for an ESP-IDF-based example firmware with a lot of features.

#### 5. Build and run on the ESP32-S3

```shell
# ESP-IDF-based firmware
make espidf-install ESPIDF_TARGET=esp32s3
make espidf-build ESPIDF_TARGET=esp32s3
make espidf-flash ESPIDF_PORT=/dev/ttyACM0
make console TTY=/dev/ttyACM0
```

#### 6. Build and run on the ESP32-C6

```shell
# ESP-IDF-based firmware
make espidf-install ESPIDF_TARGET=esp32c6
make espidf-build ESPIDF_TARGET=esp32c6
make espidf-flash ESPIDF_PORT=/dev/ttyUSB0
make console TTY=/dev/ttyUSB0
```

---

### Documentation

Check out the [docs](docs) directory for more information about the supported boards and my notes on them.

See the [experiments](experiments) directory for information about experiments using the microcontrollers.

---

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for details on how to make a contribution.

### License

The content of this repository is licensed under the Apache License, Version 2.0 (see [LICENSE](LICENSE)).
