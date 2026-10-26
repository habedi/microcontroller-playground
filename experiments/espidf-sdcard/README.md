## ESP-IDF Workstation Shell

ESP-IDF experiment providing an interactive NSH-compatible command shell with filesystem storage, wireless networking, and an in-terminal text editor across the ESP32-P4, the ESP32-S3, and the ESP32-C6.

- Boards: Waveshare ESP32-P4-WIFI6 (chip revision v1.3), Waveshare ESP32-S3-Zero, Espressif ESP32-C6-DevKitC-1-N8
- OS: FreeRTOS, through ESP-IDF v6.1
- Storage: 128 GB MicroSD card at `/sdcard` (FATFS) and 12 MB LittleFS at `/data` on ESP32-P4; 2.375 MB LittleFS at `/data` on ESP32-S3 and ESP32-C6
- Wireless: Wi-Fi STA stack driven through onboard ESP32-C6 over SDIO on ESP32-P4; native on-chip 2.4 GHz Wi-Fi on ESP32-S3; native on-chip 2.4 GHz Wi-Fi 6 on ESP32-C6
- Console: interactive REPL with `nsh> ` prompt on UART0 (ESP32-P4, ESP32-C6) or USB Serial/JTAG (ESP32-S3)

### Hardware Configuration

ESP32-P4:

- MicroSD slot: Host Slot 0 with CLK on GPIO 43, CMD on GPIO 44, D0 through D3 on GPIO 39 through 42, active-low power switch on GPIO 45, and LDO channel 4 pad power.
- Internal storage: 12 MB LittleFS partition on onboard NOR flash at offset `0x400000`.
- Wireless coprocessor: onboard ESP32-C6 on SDIO Host Slot 1 (CLK on GPIO 18, CMD on GPIO 19, D0 through D3 on GPIO 14 through 17, and reset on GPIO 54).
- Console: external CH343 UART0 bridge.

ESP32-S3 (Waveshare ESP32-S3-Zero):

- Internal storage: 2.375 MB LittleFS partition on onboard 8 MB NOR flash at offset `0x5a0000`.
- Wireless: native on-chip 2.4 GHz 802.11 b/g/n radio.
- Console: internal USB Serial/JTAG controller routed directly to the USB-C port.

ESP32-C6 (Espressif ESP32-C6-DevKitC-1-N8):

- Internal storage: 2.375 MB LittleFS partition on onboard 8 MB NOR flash at offset `0x5a0000`.
- Wireless: native on-chip 2.4 GHz 802.11ax/b/g/n Wi-Fi 6 radio.
- Console: Silicon Labs CP2102N UART bridge on UART0 (`/dev/ttyUSB0`) or internal USB Serial/JTAG (`/dev/ttyACM0`).

### Interactive Commands

The firmware runs `esp_console` (UART0 on ESP32-P4 and ESP32-C6, USB Serial/JTAG on ESP32-S3) with prompt `nsh> `.
Readline support provides command history with Up/Down arrows, in-line editing with Left/Right arrows, and Tab autocompletion for both command names and file paths across `/sdcard` and `/data`.
Path completion accepts quoted or escaped spaces and escapes suggested filenames for the command parser.
Suggestions that exceed the console line limit are omitted.

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
- `retawq <url>`: launch the retawq text web browser (or `retawq --dump=<url>`)
- `browse <url>`: alias for `retawq` web browser
- `wget -O <file|-> <url>`: download an HTTP or HTTPS URL to a file, or print its body with `-O -`
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
- `reset_c6`: pulse hardware reset to the ESP32-C6 coprocessor (ESP32-P4 only)

### HTTPS

Retawq uses mbedTLS for HTTPS, with hostname and certificate date checks enabled.
The ESP-IDF certificate bundle supplies trusted public root certificates.
Cross-signed certificate verification is enabled for chains such as Google's.
ECDSA signature verification runs in software because the hardware peripheral is unavailable on silicon revision v1.3.
Use an explicit HTTPS URL:

```text
retawq https://example.com
retawq --dump=https://example.com
```

The firmware starts SNTP through `pool.ntp.org` after Wi-Fi gets an IP address.
If the clock is still unset, the browser command waits up to 15 seconds for synchronization.
It returns an error if synchronization fails; certificate checks stay enabled.

### File Downloads

The `wget` command uses ESP-IDF's HTTP client, follows redirects, and verifies HTTPS certificates.
The `-O` output argument and an explicit HTTP or HTTPS URL are required.
Relative output paths use the shell's current directory.

```text
wget -O /data/example.html https://example.com
wget -O - https://example.com
```

Downloads stream to a file inside a private staging directory beside the destination and are published only after the response is complete.
Network errors, HTTP error responses, and write failures leave an existing destination unchanged.
Console output streams directly and can contain a partial response if the request fails.
This command implements only the `-O` download form.
On FAT, replacing an existing file moves the old output into the staging directory because FAT cannot replace files with `rename`.
The moved output is checked through its file descriptor before publishing the download.
If replacement and recovery both fail, the command prints the backup path so the old file can be recovered.
A power loss during replacement can leave a `<output>.partXXXXXX` directory containing `old`, `new`, or both; replacement on FAT is not atomic.

### Build and Flash

Build for ESP32-P4 (default):

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32p4
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32p4 ESPIDF_PORT=/dev/ttyACM0
```

Build for ESP32-S3:

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32s3
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32s3 ESPIDF_PORT=/dev/ttyACM0
```

Build for ESP32-C6:

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32c6
make espidf-flash ESPIDF_SRC=experiments/espidf-sdcard ESPIDF_TARGET=esp32c6 ESPIDF_PORT=/dev/ttyUSB0
```

Connect via serial console (`make console TTY=/dev/ttyUSB0` or `TTY=/dev/ttyACM0`) or through the `board` MCP server.

### Host Tests

Run `make -C experiments/espidf-sdcard/test test` inside the Nix dev shell.
The tests use address and undefined-behavior sanitizers.
They check path resolution, status rendering with long filenames, and loading long lines with LF or CRLF endings.
Carriage returns inside a line, and at the end of a file without a newline is preserved.
These fixes passed host tests; they have not been verified on the board.
The Wi-Fi credential fixes also passed an ESP32-P4 firmware build in the Nix dev shell.
Autocomplete tests use the parser and line editor from the `external/esp-idf` submodule.
They cover quoted paths, escaped filenames, long prefixes, and accepting a completion followed by Backspace.
The autocomplete fixes passed sanitized host tests and an ESP32-P4 firmware build; they have not been verified on the board.
Download tests cover binary bodies, redirects, incomplete transfers, HTTP errors, and output failures.
The `wget` host tests and ESP32-P4 firmware build passed; the new command has not been flashed or verified on hardware.
Regression tests cover editor allocation failures, download replacement with FAT rename rules, and failed replacement recovery.
Download race tests replace the destination with a directory, symlink, or FIFO before the backup move.
They also check that a competing destination is preserved and that the previous output remains recoverable.
The shell copy tests run the SDK's FatFS on a RAM disk with the repository's filename and locking options.
They check case aliases, short filename aliases, source preservation, and the home directory for each board.
`CONFIG_FATFS_FS_LOCK=6` prevents writes through aliases of an open source file and allows the six directory levels used by `tree`.
Run `make espidf-distclean ESPIDF_SRC=experiments/espidf-sdcard` before rebuilding an existing build directory to apply the new default.
These regression tests and clean ESP32-P4, ESP32-S3, and ESP32-C6 firmware builds passed.
The image was flashed to the standalone ESP32-C6-DevKitC-1, and flash hash verification passed.
The shell booted with `/data` mounted, and bare `cd` succeeded on the C6.
The S3 image was also flashed with hash verification, and its shell and bare `cd` check passed over native USB.
The P4 image was flashed with hash verification, and both filesystems mounted.
Bare `cd` restored `/sdcard` from `/data`.
An SD card copy through a case alias was rejected with the source preserved, and an ordinary copy succeeded with matching contents.
Temporary verification files were removed; the onboard C6 wireless firmware was not changed.
Editor allocation failures and FAT download replacement recovery remain covered by host tests.

Run `make -C experiments/espidf-sdcard/test test-retawq-tls` for the HTTPS tests.
This target uses uv's `test` dependency group for mbedTLS's Python code generators.
It builds the SDK's mbedTLS sources in `build/retawq-tls-test`, leaving the submodule unchanged.
Local HTTPS servers check trusted certificates, expired certificates, untrusted certificates, hostname mismatches, and buffered records.
The tests also check nonblocking read and write retries.
Leak detection is disabled for these browser tests because retawq's existing teardown does not release all browser state.
The HTTPS tests and ESP32-P4 firmware build passed.
The image was flashed on the Waveshare ESP32-P4-WIFI6, and flash hash verification passed.
The serial shell returned, but `wlan0` was down with no IP address.
After reconnecting with the saved Wi-Fi credentials, the HTTPS page dump from example.com succeeded.
Google initially failed certificate verification because its chain uses a cross-signed root.
With cross-signed verification enabled, `retawq --dump=https://google.com` returned its redirect page.
The interactive `retawq https://google.com` command displayed Google without a TLS error, and quitting returned to the shell.
