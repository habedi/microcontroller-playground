## Microcontroller Playground

[![Tests](https://img.shields.io/github/actions/workflow/status/habedi/microcontroller-playground/tests.yml?label=tests&style=flat&labelColor=282c34&logo=github&logoColor=white)](https://github.com/habedi/microcontroller-playground/actions/workflows/tests.yml)
[![Documentation](https://img.shields.io/badge/docs-latest-007ec6?style=flat&labelColor=282c34&logo=read-the-docs&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/docs)
[![License](https://img.shields.io/badge/license-Apache--2.0-007ec6?style=flat&labelColor=282c34&logo=open-source-initiative&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/LICENSE)
[![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-Pico%202-507ec6?style=flat&labelColor=282c34&logo=raspberrypi&logoColor=white)](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
[![Apache NuttX](https://img.shields.io/badge/Apache-NuttX-507ec6?style=flat&labelColor=282c34&logo=apache&logoColor=white)](https://nuttx.apache.org/)

---

This is a playground for my microcontroller projects and experiments.

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
> Espressif boards also needs `make install` for esptool.

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
make nuttx-configure BOARD=esp32p4-function-ev-board:nsh
make nuttx-build
make nuttx-flash-esp PORT=/dev/ttyACM0
make console TTY=/dev/ttyACM0
```

> [!NOTE]
> The onboard WCH CH343 bridge links UART0 (GPIO 37 and GPIO 38) to `/dev/ttyACM0`.

> [!NOTE]
> ESP32-P4 silicon revisions below v3.0 need configuring via
> `make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/esp32p4-function-ev-board/nsh-rev1` to bypass the revision check.
> Serial access needs membership in the `dialout` group (can be added by running `sudo usermod -aG dialout $USER`).

---

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for details on how to make a contribution.

### License

The content of this repository is licensed under the Apache License, Version 2.0 (see [LICENSE](LICENSE)).
