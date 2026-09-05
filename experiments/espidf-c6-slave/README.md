## ESP-IDF C6 Coprocessor Firmware

ESP-Hosted coprocessor firmware for the ESP32-C6 on the ESP32-P4 function EV board. This is the half that runs
on the radio chip and answers the P4 over SDIO.

- Board: the ESP32-C6-MINI-1 module on the ESP32-P4 function EV board
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Result: builds, 640 KB application. Not yet flashed, because that needs a UART adapter on the `PROG_C6`
  header.


### The Board Is Not Espressif's Reference

This board is a Waveshare ESP32-P4-WIFI6, not the ESP32-P4-Function-EV-Board that earlier notes named. It has no
`PROG_C6` header. Waveshare exposes the C6 as "ESP32-C6 UART Pads", so any mention of that header below describes
Espressif's board and not this one. Reaching the C6 here needs a soldered connection.

### Why This Exists

The C6 ships with ESP-Hosted slave firmware v0.0.6 and does not enumerate as an SDIO card. Three host
generations fail identically, so the version pairing is the leading suspect. See
[../espidf-wifi](../espidf-wifi). Building the coprocessor half ourselves removes the guesswork: both sides then
come from `esp_hosted` 3.0.6 rather than from a factory image of unknown vintage.

### Build

```shell
make espidf-install ESPIDF_TARGET=esp32c6
make espidf-build ESPIDF_SRC=experiments/espidf-c6-slave ESPIDF_TARGET=esp32c6
```

The toolchain is the same `riscv32-esp-elf` the ESP32-P4 uses, so the install step costs almost nothing after
the P4 target is in place.

### Flash

```shell
make espidf-flash-c6 C6_PORT=/dev/ttyUSB0
```

Wire the adapter to the `PROG_C6` header: `ESP_EN`, `ESP_TXD`, `ESP_RXD`, and `GND`. Do not connect VDD. Hold
the P4 in its bootloader, by holding BOOT, pressing and releasing RST, then releasing BOOT, so it does not drive
the shared bus while the C6 is being written.

The adapter appears as `/dev/ttyUSB0`, which cannot collide with the `/dev/ttyACM0` the P4 uses.

The layout, from the build's own `flash_args`, differs from the P4's because the C6 puts its bootloader at zero:

| Offset | Image |
| --- | --- |
| 0x0 | bootloader.bin |
| 0x8000 | partition-table.bin |
| 0x10000 | espidf_c6_slave.bin |

### Configuration

```
CONFIG_ESP_HOSTED_CP=y
CONFIG_EH_TRANSPORT_CP_SDIO=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
```

`ESP_HOSTED_CP` selects the coprocessor role, since the component defaults to host. The SDIO transport is
already the default on a chip with an SDIO slave, and pinning it means a future change of default cannot
quietly produce firmware that speaks the wrong bus.

The SDIO pins need no configuration. The component defaults to CMD 18, CLK 19, and D0 through D3 on 20 to 23,
which are the C6's own fixed SDIO slave pins, so they are a property of the chip rather than of the board. They
are unrelated to the P4-side numbers, which are CLK 18, CMD 19, and D0 through D3 on 14 to 17.

The console is on the C6's UART0, which the same `PROG_C6` header exposes, so the chip's own boot log is
readable with the adapter already wired for flashing. That log is the first thing to look at, before flashing
anything: silence and a boot log point at different faults.

### What Is Untested

Everything past the build. Whether the C6 accepts the firmware, whether it then enumerates, and whether the P4
reaches it are all open until the adapter arrives.
