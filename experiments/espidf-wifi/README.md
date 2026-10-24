## ESP-IDF Wi-Fi

Attempts to scan for Wi-Fi networks from the ESP32-P4, which has no radio of its own and must reach the
onboard ESP32-C6 over SDIO.

- Board: ESP32-P4 function EV board, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Result: working. Waveshare shipped the onboard ESP32-C6 with blank flash, which held the chip in a watchdog reboot loop. Flashing official Espressif ESP-Hosted SDIO slave firmware v3.0.10 through the `H4` UART pads established the SDIO link. Wi-Fi network scanning from the ESP32-P4 succeeds over SDIO at 40 MHz.


### Hardware Architecture and Pinout

The ESP32-P4 has no onboard radio. The Waveshare ESP32-P4-WIFI6 pairs it with an ESP32-C6-MINI-1 coprocessor connected over SDIO Host Slot 1:

- SDIO signals: CLK on GPIO 18, CMD on GPIO 19, and D0 through D3 on GPIO 14 through 17.
- Reset control: GPIO 54 on the ESP32-P4 drives the C6 hardware enable line (`CHIP_PU`) through resistor R83. The physical RST button on the board resets only the ESP32-P4, not the ESP32-C6.
- Programming pads: unpopulated 4-hole cluster labeled `H4` directly above the USB-C connector. From left to right:
  - Hole 1 (square pad): `IO9` (strapping pin; hold to GND on boot for download mode)
  - Hole 2 (round pad): `GND`
  - Hole 3 (round pad): `RXD` (connect to adapter TXD)
  - Hole 4 (round pad): `TXD` (connect to adapter RXD)

### Factory Defect and Diagnosis

Reading the C6 serial console through `H4` at 115200 baud revealed continuous bootloader crashes:

```
rst:0x7 (TG0_WDT_HPSYS),boot:0xc (SPI_FAST_FLASH_BOOT)
invalid header: 0xffffffff
```

The flash memory was unprogrammed (`0xFF`), so no slave firmware was present from the factory. Because watchdog resets (`rst:0x7`) do not re-sample strapping pins, the C6 remained stuck in flash boot mode until the ESP32-P4 pulsed GPIO 54 with Hole 1 held to ground, placing the C6 into ROM download mode.

### Flashing the Coprocessor

Official Espressif `esp_hosted` v3.0.10 slave firmware was built from source for target `esp32c6` using the SDIO transport. Flashing was performed with `tools/flash_official_c6.py` at 230400 baud:

- `0x00000000`: `bootloader.bin` (22 KB)
- `0x00008000`: `partition-table.bin` (3 KB)
- `0x0000d000`: `ota_data_initial.bin` (8 KB)
- `0x00010000`: `eh_cp_wifi_scan.bin` (1.09 MB)

### Verification

With Hole 1 disconnected from ground, the ESP32-P4 boots and pulses GPIO 54. The C6 boots into the newly written slave firmware, negotiates 4-bit SDIO at 40 MHz, and establishes RPC channels with the host:

```
eh_sdio: transport[host]: SDIO 4-bit 40000 kHz CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17 RESET=54
eh_init_evt: RPC version negotiated: V2
eh_init_evt: esp-hosted fw versions: host=3.0.10 coprocessor=3.0.10 (match)
eh_init_evt: SDIO SW_AGGR negotiated (e2h=15872B h2e=15872B)
espidf-wifi: radio is up, the C6 is answering
```

The Wi-Fi scan completes successfully and returns visible access points in range.

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

### Prior State and Root Cause

Before flashing the C6 coprocessor, the SDIO transport failed at CCCR register read:

```
sdmmc_io: sdmmc_io_read_byte: sdmmc_io_rw_direct (read 0x3) returned 0x107
eh_host_port_sdio: sdio_card_fn_init failed
eh_sdio: card init failed
eh_sdio: ensure_slave_bus_ready failed: -1
```

Connecting a 3.3 V USB-to-UART adapter to the `H4` pads confirmed the root cause: the onboard flash on the ESP32-C6 was entirely unprogrammed (`0xFF`), causing an infinite watchdog loop. Writing the official Espressif `esp_hosted` SDIO slave firmware resolved the timeout and enabled normal network operations.

### Program Design

`app_main()` deliberately does not call `ESP_ERROR_CHECK` on `esp_wifi_init()`.
If initialization fails, it logs the error and remains running in an idle loop rather than triggering an immediate restart, ensuring diagnostic output remains readable.
