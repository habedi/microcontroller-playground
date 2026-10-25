## RGB Cycle

This experiment runs Rust with the ESP-IDF framework on the Waveshare ESP32-S3-Zero (ESP32-S3 dev board mini 8MB PSRAM). It cycles the onboard WS2812 addressable RGB LED through seven colors once per second while logging memory statistics over the native USB Serial/JTAG console.

### Hardware

- Board: Waveshare ESP32-S3-Zero (ESP32-S3FH8R8).
- CPU: Dual-core 32-bit Xtensa LX7 up to 240 MHz.
- Memory: 8 MB Quad-SPI flash and 8 MB Octal-SPI PSRAM.
- Peripherals: Onboard WS2812 RGB LED on GPIO 21, native USB Serial/JTAG on `/dev/ttyACM0`.

### Build

The project requires the Xtensa Rust compiler (`esp` channel) and `ldproxy`. When entering the Nix development shell, the environment is automatically set up if `espup` has run:

```shell
make shell
```

From the repository root:

```shell
make idfrs-build IDFRS_SRC=experiments/rgb-cycle
```

Or build directly inside the experiment directory:

```shell
cd experiments/rgb-cycle
cargo build --release
```

The compiled ELF binary is located at `build/rust/xtensa-esp32s3-espidf/release/rgb-cycle`.

### Flash and Verify

Verify the board's USB vendor identifier before flashing:

```shell
udevadm info -q property -n /dev/ttyACM0 | grep ID_VENDOR_ID
```

Expected vendor ID: `303a` (Espressif).

Flash and monitor the board using `espflash`:

```shell
espflash flash --monitor build/rust/xtensa-esp32s3-espidf/release/rgb-cycle
```

Or use the Makefile helper:

```shell
make idfrs-flash IDFRS_PORT=/dev/ttyACM0
```

Or run via Cargo:

```shell
cd experiments/rgb-cycle
cargo run --release
```

If the board fails to enter the bootloader automatically, hold the BOOT button (GPIO 0), press and release RESET, and then release BOOT.

### Expected Behavior

1. The onboard WS2812 RGB LED cycles every second through Red, Yellow, Green, Cyan, Blue, Magenta, and White.
2. The serial console prints banner information, initial SRAM and PSRAM capacity, and ongoing cycle stats:

```
[Cycle 000] Color: Red      (R:32, G:00, B:00) | SRAM: 332 KB free | PSRAM: 8192 KB free
[Cycle 000] Color: Yellow   (R:24, G:24, B:00) | SRAM: 332 KB free | PSRAM: 8192 KB free
...
```

### Status

The firmware builds cleanly with ESP-IDF v5.3.3 and the Xtensa Rust compiler. Verification on physical hardware requires user authorization to flash over the serial port.
