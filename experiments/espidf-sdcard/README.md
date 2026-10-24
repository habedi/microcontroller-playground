## ESP-IDF SD Card Test

ESP-IDF experiment testing the onboard MicroSD / TF card slot on the Waveshare ESP32-P4-WIFI6.

- Board: Waveshare ESP32-P4-WIFI6, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Hardware link: 4-bit SDMMC host interface with active-low power gating on GPIO 45

### Hardware Wiring

The TF card slot on the Waveshare board links to the ESP32-P4 SDMMC controller pins:

- CLK: GPIO 43
- CMD: GPIO 44
- D0: GPIO 39
- D1: GPIO 40
- D2: GPIO 41
- D3: GPIO 42
- Power switch: GPIO 45 (drives an onboard P-channel MOSFET switch; set to 0 to supply 3.3 V to the slot)

### Build

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard
```

### Flash and Run

```shell
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard
make espidf-monitor ESPIDF_SRC=experiments/espidf-sdcard
```

### Notes

1. The experiment drives GPIO 45 low and waits 150 ms for the power rail to stabilize before probing the card.
2. The card is initialized in 4-bit SD mode at the standard 20 MHz bus frequency.
3. ESP-IDF FatFS expects FAT or FAT32. High-capacity cards (64 GB or 128 GB) formatted as exFAT from the factory need to be formatted as FAT32 before mounting.
