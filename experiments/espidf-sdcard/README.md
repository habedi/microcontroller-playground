## ESP-IDF SD Card Test

ESP-IDF experiment testing the onboard MicroSD / TF card slot on the Waveshare ESP32-P4-WIFI6, featuring an interactive NSH-compatible command shell.

- Board: Waveshare ESP32-P4-WIFI6, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Hardware link: 4-bit SDMMC host interface (Slot 0), on-chip LDO channel 4 for IO pads, and active-low power switch on GPIO 45
- Result: passes. Probes 128 GB card (121.9 GB usable FAT32 volume), mounts at `/sdcard`, provides an interactive REPL shell with `nsh> ` prompt.

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

### Interactive Commands

The firmware runs `esp_console` on UART0 with prompt `nsh> `:

- `help`: list available commands
- `pwd`: print current working directory
- `cd [<path>]`: change working directory
- `ls [<path>]`: list files and directories
- `tree [<path>]`: display recursive directory hierarchy with file sizes
- `cat <filename>`: display file contents
- `write <filename> <text>`: write text to a file
- `cp <source> <destination>`: copy a file
- `rm <filename>`: delete a file
- `mkdir <dirname>`: create directory
- `df`: display storage capacity, used space, and free space
- `ps`: list running FreeRTOS tasks with priority, state, stack margin, and core affinity
- `free`: display heap memory statistics
- `uname`: display operating system, chip revision, and core count
- `reboot`: restart the board

### Build and Flash

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard
```

Connect via serial terminal (`picocom -b 115200 /dev/ttyACM0`) or through the `board` MCP server.
