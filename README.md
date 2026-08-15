## Microcontroller Playground

[![Tests](https://img.shields.io/github/actions/workflow/status/habedi/microcontroller-playground/tests.yml?label=tests&style=flat&labelColor=282c34&logo=github&logoColor=white)](https://github.com/habedi/microcontroller-playground/actions/workflows/tests.yml)
[![Documentation](https://img.shields.io/badge/docs-latest-007ec6?style=flat&labelColor=282c34&logo=read-the-docs&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/docs)
[![License](https://img.shields.io/badge/license-Apache--2.0-007ec6?style=flat&labelColor=282c34&logo=open-source-initiative&logoColor=white)](https://github.com/habedi/microcontroller-playground/blob/main/LICENSE)

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
make install   # The uv-managed Python tools, like esptool and pre-commit
```

#### 3. Configure and build NuttX

```shell
make nuttx-configure           # Or make nuttx-configure BOARD=raspberrypi-pico-2:nsh
make nuttx-build
```

#### 4. Flash the image and open the serial console

```shell
make nuttx-flash-esp PORT=/dev/ttyUSB0   # An Espressif board, over its serial port
make flash-pico                          # The Pico 2, over USB with picotool
make console TTY=/dev/ttyACM0            # Picocom at 115200 baud; quit with C-a C-x
```

> [!NOTE]
> Serial access needs membership in the `dialout` group.
> Run `sudo usermod -aG dialout $USER`, then log in again, if you are not a member of the group.

---

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for details on how to make a contribution.

### License

The content of this repository is licensed under the Apache License, Version 2.0 (see [LICENSE](LICENSE)).
