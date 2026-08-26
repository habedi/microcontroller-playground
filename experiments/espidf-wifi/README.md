## ESP-IDF Wi-Fi

Attempts to scan for Wi-Fi networks from the ESP32-P4, which has no radio of its own and must reach the
onboard ESP32-C6 over SDIO.

- Board: ESP32-P4 function EV board, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Result: blocked. The host software is correct and the SDIO transport starts, but the C6 never answers, so
  `esp_wifi_init()` fails. The C6 needs new slave firmware, which needs a UART adapter on the `PROG_C6` header.


### The Board Is Not Espressif's Reference

This board is a Waveshare ESP32-P4-WIFI6, not the ESP32-P4-Function-EV-Board that earlier notes named. It has no
`PROG_C6` header. Waveshare exposes the C6 as "ESP32-C6 UART Pads", so any mention of that header below describes
Espressif's board and not this one. Reaching the C6 here needs a soldered connection.

### Build and Run

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-wifi
make espidf-flash ESPIDF_SRC=experiments/espidf-wifi
make espidf-monitor ESPIDF_SRC=experiments/espidf-wifi
```

### What Happens

The transport starts and finds the right pins without being told:

```
eh_sdio: transport[host]: SDIO 4-bit 40000 kHz CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17 RESET=54
eh_sdio: SDIO clock freq set to [20000]KHz, Max possible (on PCB) is 40000KHz
eh_sdio: Reset co-processor using GPIO[54]
```

Those match the pins Espressif documents for this board, so the component recognises the hardware. The C6 then
fails to enumerate:

```
sdmmc_io: sdmmc_io_read_byte: sdmmc_io_rw_direct (read 0x3) returned 0x107
eh_host_port_sdio: sdio_card_fn_init failed
eh_sdio: card init failed
eh_sdio: ensure_slave_bus_ready failed: -1
```

`0x107` is `ESP_ERR_TIMEOUT`, and the failing operation is a CMD52 read of register `0x3`, which is ordinary
SDIO enumeration rather than anything specific to ESP-Hosted. The C6 is not presenting as an SDIO card at all.

### What Was Ruled Out

Clock speed. 40 MHz and 20 MHz fail identically.

Host software generation. Three of them fail identically: `esp_hosted` 3.0.6, 2.7.4, and 1.4.7. The last is
what Waveshare pins for ESP-IDF 5.x in its own working ESP32-P4 Wi-Fi example, at
https://github.com/waveshareteam/esp32-p4-platform, paired with `esp_wifi_remote` 0.14.5. Every one of them
fails on the same call, a CMD52 read of CCCR register `0x3` on function 0, which 1.4.7 turns into an assertion
and a boot loop:

```
sdio_wrapper.c line 135, hosted_sdio_card_fn_init
expression: sdmmc_io_read_byte(card, SDIO_FUNC_0, SD_IO_CCCR_FN_READY, &ior)
```

Three generations spanning two years of the component's history agree, so this is not a version mismatch on the
host side.

Configuration. The component sets `RESET_GPIO=54`, SDIO slot 1, 4-bit width, and the correct pins by itself,
matching the board documentation.

### The Likely Cause

The C6 ships with ESP-Hosted slave firmware v0.0.6, and Espressif recommends upgrading it. A slave that old
paired with a 3.x host is the obvious suspect, and failing at SDIO enumeration is consistent with the C6 not
running usable slave firmware.

This failure is widely reported on ESP32-P4 and ESP32-C6 boards, including
https://github.com/espressif/esp-hosted-mcu/issues/66,
https://github.com/espressif/esp-hosted-mcu/issues/167, and
https://github.com/espressif/arduino-esp32/issues/11404. One report describes a board shipped without the slave
firmware loaded at all.

### The Cable Diagnoses as Well as Repairs

Wiring a UART to the `PROG_C6` header is worth doing even before flashing anything, because reading the C6's
own console distinguishes cases that look identical from the P4 side. Silence means the chip is not running.
A boot log means it runs but is not presenting as an SDIO slave. Either answer narrows the problem in a way
nothing done from the host can.

### What Would Unblock It

Flashing the C6 with matching slave firmware. Espressif documents two routes:

- Over the air from the P4. This needs no extra hardware but does need a working link, so it cannot bootstrap
  from here.
- Serially through the `PROG_C6` header, wiring `ESP_EN`, `ESP_TXD`, `ESP_RXD`, and `GND`, and explicitly not
  VDD, with the P4 held in its bootloader so it does not interfere. This needs a 3.3 V USB to UART adapter.

The second is the only one available from this state, which restores the USB to UART cable in
`docs/tools-to-buy.md` to a required item for this board rather than an optional one.

### A Note on the Program

`app_main()` deliberately does not call `ESP_ERROR_CHECK` on `esp_wifi_init()`. The first version did, which
turned a failed probe into a panic and a boot loop, so the diagnosis scrolled past before it could be read. An
experiment that exists to test whether something works should report the failure and stay alive.
