## Microcontroller Playground

[![Tests](https://img.shields.io/github/actions/workflow/status/habedi/microcontroller-playground/tests.yml?label=tests&style=flat&labelColor=282c34&logo=github&logoColor=white)](https://github.com/habedi/microcontroller-playground/actions/workflows/tests.yml)
[![Documentation](https://img.shields.io/badge/docs-latest-007ec6?style=flat&labelColor=282c34&logo=read-the-docs&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/docs)
[![License](https://img.shields.io/badge/license-Apache--2.0-007ec6?style=flat&labelColor=282c34&logo=open-source-initiative&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/LICENSE)
[![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-Pico%202-c51a4a?style=flat&labelColor=282c34&logo=raspberrypi&logoColor=white)](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
[![Apache NuttX](https://img.shields.io/badge/Apache-NuttX-d22128?style=flat&labelColor=282c34&logo=apache&logoColor=white)](https://nuttx.apache.org/)

---

This is a playground for my microcontroller projects and experiments.

---

### Getting Started

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
> The first `make shell` builds the RISC-V cross compiler toolchain from source, which can take some time.
> When the first build is done, run `make shell-pin`, so a Nix garbage collection does not discard the build results.
> The Espressif boards also need `make install`, which installs esptool. Note that the Raspberry Pi Pico 2 does not need it.

#### 3. Build and run on the Pico 2

```shell
make nuttx-configure BOARD=raspberrypi-pico-2:usbnsh
make nuttx-build
make flash-pico-uf2             # Hold BOOTSEL (button on the board) while plugging the board in
make console TTY=/dev/ttyACM0   # Picocom at 115200 baud; quit with C-a C-x
```

Press Enter after the console opens to get the `nsh>` prompt.

<div align="center">
  <img alt="NuttShell" src="docs/assets/images/pico2w-nsh-1.jpeg" width="100%">
</div>

The `usbnsh` configuration puts the NuttShell on the USB cable, so no extra hardware is needed.
The `nsh` configuration puts it on UART0 (at GP0 and GP1 pins), which needs a USB to UART adapter.

Both flash targets need the board in BOOTSEL mode.
`make flash-pico-uf2` copies the image to the drive that the board exposes, and needs no special permissions.
`make flash-pico` uses picotool, which needs the udev rules that `make setup-udev` installs once.

#### 4. Build and run on the ESP32-P4

```shell
make nuttx-distclean            # Run this whenever you switch boards
make nuttx-configure BOARD=esp32p4-function-ev-board:nsh
make nuttx-build
make nuttx-flash-esp PORT=/dev/ttyACM0
make console TTY=/dev/ttyACM0
```

The USB-C port on this board is wired to the chip's own USB Serial/JTAG controller, so it appears as `/dev/ttyACM0`.
Boards that use a CP2102 bridge instead appear as `/dev/ttyUSB0`.

> [!NOTE]
> An ESP32-P4 with a silicon revision below v3.0 halts in the revision check that the stock configuration compiles in.
> Configure such a chip from the saved configuration instead, with
> `make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/esp32p4-function-ev-board/usbconsole-rev1`.
> It then boots NuttX, but the USB console does not reach a shell prompt.

> [!NOTE]
> Serial access needs membership in the `dialout` group.
> Run `sudo usermod -aG dialout $USER`, then log in again.
> If you are not a member of the group, you will not have permission to access the serial port.

---

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for details on how to make a contribution.

### License

The content of this repository is licensed under the Apache License, Version 2.0 (see [LICENSE](LICENSE)).
