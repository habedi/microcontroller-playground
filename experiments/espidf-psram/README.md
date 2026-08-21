## ESP-IDF PSRAM

Tests whether the 32 MB of PSRAM on the ESP32-P4 function EV board works, on a chip revision that reports
faults elsewhere.

- Board: ESP32-P4 function EV board, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Result: works. 32 MB detected and added to the heap, at both 20 MHz and 200 MHz, with a pattern test passing
  at each.

### Build and Run

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-psram
make espidf-flash ESPIDF_SRC=experiments/espidf-psram
make espidf-monitor ESPIDF_SRC=experiments/espidf-psram
```

### What It Reports

```
I (375) esp_psram: Found 32MB PSRAM device
I (375) esp_psram: Speed: 200MHz
I (1327) esp_psram: SPI SRAM memory test OK
I (1411) esp_psram: Adding pool of 32768K of PSRAM memory to heap allocator
espidf-psram on chip revision v1.3
PSRAM initialised: yes
PSRAM size: 33554432 bytes
free internal: 588359 bytes
free spiram:   33551716 bytes
pattern test: PASS
```

The driver identifies an AP Memory generation 4 part, 256 Mbit, in X16 mode, reporting `good-die`. At 200 MHz
the boot log adds a timing tuning step, and the built-in memory test finishes in about a quarter of the time it
takes at 20 MHz.

### Why the Pattern Test

ESP-IDF runs its own `SPI SRAM memory test` during startup, and the driver reporting a size proves only that
the controller answered. This program allocates 1 MB from the PSRAM heap, writes a value derived from each
word's index, then reads every word back. That catches memory that initialises but does not hold data, which is
the failure mode a size report would hide.

### Configuration

`sdkconfig.defaults` holds the revision options, the UART console, and:

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y
```

200 MHz is the maximum available here. `CONFIG_SPIRAM_SPEED_250M` carries
`depends on !ESP32P4_SELECTS_REV_LESS_V3`, so the same switch that lets this revision boot removes the 250 MHz
option. 200 MHz is the default and the board's rated speed.

Nothing needs to be done about power. ESP-IDF reserves LDO channel 2 at 1.8 V for PSRAM by default.

### What This Settles

An ESPHome report against this same board and revision describes a load access fault at `0x5008e1a0` whenever
any PSRAM configuration was present. See https://github.com/esphome/esphome/issues/16903. It does not
reproduce here under ESP-IDF v5.5.5, so the silicon can drive its PSRAM.
