## ESP-IDF Hello

Smallest ESP-IDF program for the ESP32-P4, written to find out whether the vendor SDK reaches a running
system on a board whose NuttX build never reaches a NuttShell prompt.

- Board: ESP32-P4 function EV board, chip revision v1.3
- OS: FreeRTOS, through ESP-IDF v5.5.5
- Result: runs. Prints its revision, 586 KB of free heap, and a counter, over the board's CH343 UART bridge.

### Build

```shell
make submodules
make espidf-install
make espidf-build
```

`make espidf-install` downloads the compilers and a Python environment into `build/espressif`, which is about
2.6 GB and runs once. It puts them inside the repository rather than in `~/.espressif`, so a checkout stays
self contained. The compiler it fetches is `riscv32-esp-elf-gcc 14.2.0` from release `esp-14.2.0_20260121`,
the same release CI installs for the NuttX ESP32-P4 job.

`ESPIDF_SRC` selects the project directory and defaults to this one. `ESPIDF_TARGET` defaults to `esp32p4`.
Output goes to `build/espidf/espidf-hello/`, outside the submodule.

### Flash and Connect

```shell
make espidf-flash
make espidf-monitor
```

`ESPIDF_PORT` defaults to `/dev/ttyACM0`, which is correct for this board, because its USB-C connector goes
to the chip's own USB Serial/JTAG controller. Leave the monitor with Ctrl-]. The program prints the chip
revision, the free heap, and a counter once per second. There is no shell and no Enter to press.

### Configuration

`sdkconfig.defaults` holds the three settings this board needs. The resolved `sdkconfig` is generated into the
build directory instead of next to the project, so `make espidf-distclean` discards it; edits worth keeping
belong in `sdkconfig.defaults`.

Two of the settings cover the silicon revision:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
```

ESP-IDF defaults to a minimum revision of v3.1, and its bootloader rejects an image whose minimum exceeds the
chip. These two move the accepted window to v1.0 through v1.99, which contains this chip's v1.3. NuttX uses
the same two names, having taken them from ESP-IDF. The Kconfig help calls support for revisions below v3.0
and from v3.0 up mutually exclusive, because of large hardware differences between them.

The third selects the console:

```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

ESP-IDF puts the console on UART0 by default, and on this board GPIO37 and GPIO38 reach an onboard WCH CH343
bridge, so that default works over the board's UART connector with no extra hardware. Keeping the chip's own
USB Serial/JTAG controller as the secondary console sends the same output to the other connector.

PSRAM stays off, since it faults on this revision under ESP-IDF as well as under NuttX. See
[../../docs/esp32p4.md](../../docs/esp32p4.md).

### Why This Exists Alongside NuttX

NuttX on this board completes `nx_start()` and then stops while attaching the USB Serial/JTAG console
interrupt, so no prompt appears. An ESPHome report describes the same board and revision booting and running
under ESP-IDF once PSRAM is disabled. Building the vendor stack separates a port problem from a silicon
problem: if this program runs, the fault is in the NuttX port.
