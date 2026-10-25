## ESP-IDF Hello

Smallest ESP-IDF program for verifying vendor SDK bringup on the ESP32-P4 and the Waveshare ESP32-S3-Zero.

- Boards:
  - Waveshare ESP32-P4-WIFI6 (ESP32-P4NRW32, chip revision v1.3).
  - Waveshare ESP32-S3-Zero (ESP32-S3FH8R8).
- OS: FreeRTOS, through ESP-IDF v6.1.
- Result: Runs. Prints its revision, free heap, and a counter once per second.

### Build

Fetch submodules and install the toolchains into `build/espressif` (self contained within the repository):

```shell
make submodules
```

For the ESP32-S3:

```shell
make espidf-install ESPIDF_TARGET=esp32s3
make espidf-build ESPIDF_TARGET=esp32s3
```

For the ESP32-P4 (the default target):

```shell
make espidf-install ESPIDF_TARGET=esp32p4
make espidf-build ESPIDF_TARGET=esp32p4
```

`ESPIDF_SRC` defaults to `experiments/espidf-hello`. Output is written to `build/espidf/espidf-hello/`.

### Flash and Connect

```shell
make espidf-flash ESPIDF_PORT=/dev/ttyACM0
make espidf-monitor ESPIDF_PORT=/dev/ttyACM0
```

`ESPIDF_PORT` defaults to `/dev/ttyACM0`. Leave the monitor with Ctrl-]. The program prints the chip revision, free heap, and an incrementing counter once per second.

### Configuration

Target-specific settings are separated into dedicated defaults files:

- `sdkconfig.defaults`: Baseline options common to all targets (PSRAM disabled for the basic heap check).
- `sdkconfig.defaults.esp32s3`: Selects `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` so console output goes to the onboard USB-C port.
- `sdkconfig.defaults.esp32p4`: Configures compatibility with silicon revision v1.3 (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and `CONFIG_ESP32P4_REV_MIN_100=y`) and routes console to UART0 for the onboard CH343 bridge.

The resolved `sdkconfig` is generated inside the build directory.

### Why This Exists Alongside NuttX

NuttX on the ESP32-P4 completes `nx_start()` and then stops while attaching the USB Serial/JTAG console interrupt. Building the vendor ESP-IDF stack confirms whether the underlying hardware and silicon function properly: if this program runs, the issue lies in the NuttX port rather than the board.
