## ESP32-S3 Notes

Notes on setting up and running Rust firmware with ESP-IDF on the Waveshare ESP32-S3-Zero mini development board.

### Hardware

The board is a Waveshare ESP32-S3-Zero (sold by Electrokit as the ESP32-S3 dev board mini 8MB PSRAM 8MB with headers):

- Chip: ESP32-S3FH8R8 (dual-core 32-bit Xtensa LX7 processor running at up to 240 MHz).
- Flash: 8 MB embedded Quad SPI (QPI) flash.
- PSRAM: 8 MB embedded Octal SPI (OPI) PSRAM.
- Wireless: 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE).
- Onboard RGB LED: WS2812 addressable LED connected to GPIO 21.
- Buttons: BOOT button connected to GPIO 0, and a hardware RESET button.
- USB port: USB Type-C connector wired directly to the internal ESP32-S3 USB Serial/JTAG controller (GPIO 19 for D-, GPIO 20 for D+).
- Form factor: Compact castellated module with 24 multi-function GPIO pins and pre-soldered headers.

### Serial Port

The USB-C connector links directly to the chip's internal USB Serial/JTAG peripheral:

- USB Vendor ID: `303a`
- USB Product ID: `1001`
- Device name: `Espressif USB JTAG/serial debug unit`
- Linux device node: `/dev/ttyACM0` (or `/dev/ttyACM1` when other USB CDC-ACM devices are plugged in).

Verify the vendor identifier before flashing:

```shell
udevadm info -q property -n /dev/ttyACM0 | grep ID_VENDOR_ID
```

The native USB Serial/JTAG interface supports direct flashing via `espflash` and logging over the serial console. If auto-reset into the bootloader fails, hold the BOOT button, tap RESET, and then release BOOT.

### Toolchain

The ESP32-S3 uses the Xtensa LX7 architecture:

- Toolchain: Espressif Xtensa Rust compiler fork (`channel = "esp"`), installed using `espup`.
- Target triple: `xtensa-esp32s3-espidf` for standard library applications running on top of ESP-IDF.
- Linker wrapper: `ldproxy` handles ESP-IDF linker argument generation.
- Standard library: Built from source via `-Z build-std=std,panic_abort`.
- Flashing tool: `espflash` (provided in the Nix development shell).

Source the Xtensa environment before building:

```shell
. $HOME/export-esp.sh
```

### ESP-IDF Configuration

The board integrates 8 MB Octal PSRAM and 8 MB flash. In `sdkconfig.defaults`, the following options enable Octal PSRAM and USB Serial/JTAG console:

```ini
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
CONFIG_FREERTOS_IDLE_TASK_STACKSIZE=4096
CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=4096

# Octal PSRAM support for ESP32-S3FH8R8
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# Route console output through the USB Serial/JTAG peripheral
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

### ESP-IDF C Firmware

The repository includes ESP-IDF v6.1 in `external/esp-idf`. Toolchains are placed inside `build/espressif` to keep checkouts self-contained.

1. Install the toolchain for ESP32-S3:

```shell
make espidf-install ESPIDF_TARGET=esp32s3
```

2. Build `experiments/espidf-hello`:

```shell
make espidf-build ESPIDF_TARGET=esp32s3
```

3. Flash and open the serial monitor:

```shell
make espidf-flash ESPIDF_PORT=/dev/ttyACM0
make espidf-monitor ESPIDF_PORT=/dev/ttyACM0
```

