## ESP-IDF C6 Flash Probe

Asks whether the onboard ESP32-C6 answers its ROM loader over SDIO, which would allow reflashing it from the
ESP32-P4 with no external hardware.

- Board: ESP32-P4 function EV board, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Result: inconclusive. `esp_loader_connect()` times out, but the bootstrap pin the test used is an audio pin
  on this board, so download mode was never entered.


### The Board Is Not Espressif's Reference

This board is a Waveshare ESP32-P4-WIFI6, not the ESP32-P4-Function-EV-Board that earlier notes named. It has no
`PROG_C6` header. Waveshare exposes the C6 as "ESP32-C6 UART Pads", so any mention of that header below describes
Espressif's board and not this one. Reaching the C6 here needs a soldered connection.

### Why This Exists

Wi-Fi and Bluetooth on this board need the C6, and the C6 does not enumerate as an SDIO card under ESP-Hosted.
See [../espidf-wifi](../espidf-wifi). Reflashing the C6 is the suspected fix, and Espressif's
`esp-serial-flasher` lists SDIO as a transport with the C6 among its supported targets, so the P4 should be able
to do it over the wires that already exist.

The difference from ESP-Hosted matters. ESP-Hosted talks to slave firmware, so it fails if that firmware is
missing or wrong. This library talks to the ROM loader, which is in mask ROM and cannot be erased, and it drives
the bootstrap as well as reset, so it can force download mode on a chip running nothing useful.

### Build and Run

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-c6flash
make espidf-flash ESPIDF_SRC=experiments/espidf-c6flash
make espidf-monitor ESPIDF_SRC=experiments/espidf-c6flash
```

### Result

```
c6flash: SDIO: CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17 RESET=54 BOOT=53
c6flash: SDIO port initialised, attempting connect
c6flash: connect failed: 2
c6flash: timeout: the C6 did not answer at all
```

Error 2 is `ESP_LOADER_ERROR_TIMEOUT`. `esp_loader_init_sdio()` succeeded, so the P4's SDIO host came up; the timeout is the absence of any reply.

### The Result Does Not Mean What It Appears To

GPIO53 is not the C6's bootstrap on this board.
Espressif's own board support package lists it among the I2S audio pins, at
https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h,
which defines I2S on GPIO 9 to 13 and 53 and mentions the C6 nowhere at all.

GPIO53 comes from this library's own example, which pairs reset on 54 with boot on 53 for an ESP32-P4 host. The
reset pin happens to be right for this board. The boot pin is not, so this test drove an audio pin and never put
the C6 into download mode. The timeout therefore says nothing about whether the ROM loader would answer.

That leaves the earlier conclusion intact rather than strengthened: the C6 boots whatever firmware it has, that
firmware does not act as an SDIO slave, and reflashing is still the likely fix.

### Cable-Free Reflashing Looks Impossible Here

ESP-Hosted drives only the reset line, because it expects working slave firmware. Forcing download mode needs
the bootstrap, and no ESP32-P4 GPIO on this board is documented as reaching it. Espressif's own procedure for
this board flashes the C6 through the `PROG_C6` header with external hardware, which is consistent with the
strap not being under P4 control.

So on this board the C6 can be reflashed through that header and, as far as the public documentation shows, not
otherwise. The over-the-air route through ESP-Hosted needs a working link and cannot bootstrap from a broken
one.

### What Would Settle It

The schematic, which would show whether any P4 GPIO reaches the C6's IO9. The v1.5.1 PDF exists, but the copies
found were unreachable, one returning 404 from Espressif's documentation host and one 403 from a mirror.

Failing that, the `PROG_C6` header. Listening on the C6's transmit line before flashing anything distinguishes
a chip that is not running from one running the wrong firmware.

### Pins

The SDIO pins are set explicitly, because the library's example targets a different board and uses GPIO 47
through 52. On this board they are CLK 18, CMD 19, D0 14, D1 15, D2 16, D3 17, with reset on 54. Leave the boot
pin out of any repeat of this test, or point it at a pin that is actually connected, since GPIO53 drives audio
hardware.
