## ESP-IDF Workstation Shell

ESP-IDF experiment providing an interactive NSH-compatible command shell on the Waveshare ESP32-P4-WIFI6 with dual filesystem storage (FATFS and LittleFS), wireless networking over SDIO, and an in-terminal text editor.

- Board: Waveshare ESP32-P4-WIFI6, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v6.1
- Storage: 128 GB MicroSD card mounted at `/sdcard` (FATFS), plus 12 MB internal flash mounted at `/data` (LittleFS)
- Wireless: Wi-Fi STA stack driven through onboard ESP32-C6 over SDIO Slot 1
- Console: interactive REPL with `nsh> ` prompt on UART0

### Hardware Configuration

- MicroSD slot: Host Slot 0 with CLK on GPIO 43, CMD on GPIO 44, D0 through D3 on GPIO 39 through 42, active-low power switch on GPIO 45, and LDO channel 4 pad power.
- Internal storage: 12 MB LittleFS partition on onboard NOR flash at offset `0x400000`.
- Wireless coprocessor: onboard ESP32-C6 on SDIO Host Slot 1 (CLK on GPIO 18, CMD on GPIO 19, D0 through D3 on GPIO 14 through 17, and reset on GPIO 54).

### Interactive Commands

The firmware runs `esp_console` on UART0 with prompt `nsh> `.
Readline support provides command history with Up/Down arrows, in-line editing with Left/Right arrows, and Tab autocompletion for both command names and file paths across `/sdcard` and `/data`.

- `help`: list available commands
- `pwd`: print current working directory
- `cd [<path>]`: change working directory across `/sdcard` and `/data`
- `ls [<path>]`: list files and directories
- `tree [<path>]`: display recursive directory hierarchy
- `cat <filename>`: display file contents
- `write <filename> <text>`: write text to a file
- `nano <filename>`: full-screen interactive VT100 text editor (shortcuts: Ctrl+S to save, Ctrl+Q to exit)
- `edit <filename>`: alias for `nano`
- `cp <source> <destination>`: copy a file between filesystems
- `rm <filename>`: delete a file
- `mkdir <dirname>`: create directory
- `df`: display storage capacity and usage for both `/sdcard` and `/data`
- `wifi_scan`: scan 2.4 GHz channels and display visible access points
- `wifi_connect [<ssid>] [<password>]`: connect to Wi-Fi with interactive masked password prompt; credentials persist in NVS and auto-connect on boot
- `wifi_disconnect`: disconnect from active Wi-Fi network
- `wifi_forget`: erase saved Wi-Fi credentials from NVS
- `ifconfig`: display network interface status and IP address
- `ping <host>`: send ICMP echo requests to network host
- `ps`: list running FreeRTOS tasks with priority, state, stack margin, and core affinity
- `top`: display live FreeRTOS tasks with CPU percentage, stack margin, and core affinity
- `free`: display heap memory statistics
- `uname`: display operating system, chip revision, and core count
- `cpuinfo`: display CPU architecture, clock frequencies, caches, and hardware features
- `ota_status`: display active partition slot, firmware version, and rollback state
- `ota_confirm`: confirm new firmware as valid and cancel rollback
- `ota_rollback`: roll back to the previous firmware slot and reboot
- `ota_update <file>`: flash firmware binary from storage to alternate OTA slot
- `reboot`: restart the board
- `reset_c6`: pulse hardware reset to the ESP32-C6 coprocessor

### Build and Flash

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard
```

Connect via serial terminal (`picocom -b 115200 /dev/ttyACM0`) or through the `board` MCP server.

### Host Tests

Run `make -C experiments/espidf-sdcard/test test` inside the Nix dev shell.
The tests use address and undefined-behavior sanitizers.
They check path resolution, status rendering with long filenames, and loading long lines with LF or CRLF endings.
Carriage returns inside a line, and at the end of a file without a newline is preserved.
These fixes passed host tests; they have not been verified on the board.
The Wi-Fi credential fixes also passed an ESP32-P4 firmware build in the Nix dev shell.
