## ESP32-C6 Notes

Notes on setting up and running firmware with ESP-IDF on the ESP32-C6-DevKitC-1 development board.

### Hardware

The board is an Espressif ESP32-C6-DevKitC-1-N8 (sold by Electrokit as the ESP32-C6-DevKitC utvecklingskort 8MB):

- Chip: ESP32-C6 (single-core 32-bit RISC-V RV32IMAC processor running at up to 160 MHz, with an LP RISC-V core at up to 20 MHz).
- Flash: 8 MB SPI flash (ESP32-C6-WROOM-1 module).
- SRAM: 512 KB HP SRAM, 16 KB LP SRAM, and 320 KB ROM.
- Wireless: 2.4 GHz Wi-Fi 6 (802.11ax), Bluetooth 5 (LE), and IEEE 802.15.4 (Zigbee and Thread).
- Onboard RGB LED: addressable LED connected to GPIO 8.
- Buttons: BOOT button connected to GPIO 9, and a hardware RESET button.
- USB ports: dual USB Type-C connectors. The UART port links to a Silicon Labs CP2102N bridge, and the USB port links directly to the internal USB Serial/JTAG peripheral.

### Serial Ports

The board provides two connection paths over USB:

- UART bridge: Silicon Labs CP2102N USB-to-UART controller wired to UART0 (GPIO 16 and 17). Vendor ID is `10c4`, product ID is `ea60`, and Linux presents `/dev/ttyUSB0`.
- Native USB: internal ESP32-C6 USB Serial/JTAG controller wired to GPIO 12 and 13. Vendor ID is `303a`, product ID is `1001`, and Linux presents `/dev/ttyACM0`.

Verify the device node and vendor identifier before flashing:

```shell
udevadm info -q property -n /dev/ttyUSB0 | grep ID_VENDOR_ID
```

The CP2102N bridge provides automated reset and bootloader strapping through DTR and RTS signals.

### Distinction From Onboard P4 Coprocessor

This standalone board connects over USB to the development machine.
It is distinct from the onboard ESP32-C6 module soldered to the Waveshare ESP32-P4-WIFI6 board.
The onboard module acts as an SDIO slave for the P4's wireless link, and its firmware must be preserved.
Flashing the standalone DevKitC-1 does not affect the P4 board.

### Toolchain

The ESP32-C6 uses the RISC-V architecture:

- Toolchain: Espressif RISC-V GCC toolchain (`riscv32-esp-elf`), installed inside `build/espressif` by `make espidf-install ESPIDF_TARGET=esp32c6`.
- Host tools: `esptool` and `espflash` provided in the Nix dev shell.

### ESP-IDF Configuration

The board integrates 8 MB flash and no external PSRAM.
In `experiments/espidf-sdcard`, target configuration resides in `sdkconfig.defaults.esp32c6`:

```ini
# Primary console on UART0 (CP2102N bridge on ESP32-C6-DevKitC-1).
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y

# Partition table and 8 MB flash size.
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_esp32c6.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions_esp32c6.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
```

The partition table (`partitions_esp32c6.csv`) defines dual 2.81 MB OTA app slots and a 2.375 MB LittleFS partition at `/data`.

### ESP-IDF C Firmware

The repository includes ESP-IDF v6.1 in `external/esp-idf`. Toolchains are kept inside `build/espressif` to keep checkouts self-contained.

1. Install the toolchain for ESP32-C6:

```shell
make espidf-install ESPIDF_TARGET=esp32c6
```

2. Build `experiments/espidf-hello`:

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-hello ESPIDF_TARGET=esp32c6
```

3. Build the workstation shell in `experiments/espidf-sdcard`:

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32c6
```

4. Flash the workstation shell over the CP2102N bridge:

```shell
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32c6 ESPIDF_PORT=/dev/ttyUSB0
```

5. Connect to the shell:

```shell
picocom -b 115200 /dev/ttyUSB0
```
