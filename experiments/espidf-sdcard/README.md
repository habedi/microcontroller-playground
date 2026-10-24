## ESP-IDF SD Card Test

ESP-IDF experiment testing the onboard MicroSD / TF card slot on the Waveshare ESP32-P4-WIFI6.

- Board: Waveshare ESP32-P4-WIFI6, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Hardware link: 4-bit SDMMC host interface (Slot 0), on-chip LDO channel 4 for IO pads, and active-low power switch on GPIO 45
- Result: passes. Probes card name, 122 GB capacity, mounts FAT filesystem at `/sdcard`, writes and reads a test file cleanly.

### Hardware Configuration

The TF card slot on the Waveshare board links to the ESP32-P4 SDMMC controller pins on Host Slot 0:

- CLK: GPIO 43
- CMD: GPIO 44
- D0: GPIO 39
- D1: GPIO 40
- D2: GPIO 41
- D3: GPIO 42
- Power switch: GPIO 45 (drives an onboard P-channel MOSFET switch; driving low supplies 3.3 V to the slot)
- IO power domain: on-chip LDO channel 4 powers the ESP32-P4 SDMMC pad domain (`sd_pwr_ctrl_new_on_chip_ldo`)

### Build and Flash

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard
```

### Verified Hardware Output

```text
I (307) espidf_sdcard: Starting ESP32-P4 SDMMC storage test
I (307) espidf_sdcard: Enabling slot power switch on GPIO 45 (active low)
I (607) espidf_sdcard: Mounting filesystem at /sdcard
I (657) espidf_sdcard: Card initialized and filesystem mounted successfully
Name: SR128
Type: SDHC
Speed: 20.00 MHz (limit: 20.00 MHz)
Size: 121942MB
CSD: ver=2, sector_size=512, capacity=249737216 read_bl_len=9
SSR: bus_width=4
I (677) espidf_sdcard: Testing file write: /sdcard/p4_test.txt
I (687) espidf_sdcard: Testing file read: /sdcard/p4_test.txt
I (687) espidf_sdcard: Read back content: ESP32-P4 SDMMC test successful
I (687) espidf_sdcard: Card read/write test passed
```
