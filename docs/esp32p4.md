## ESP32-P4 Notes

Notes on bringing up Apache NuttX on the ESP32-P4 function EV board, with 32 MB of PSRAM, headers, and an
onboard ESP32-C6 wireless coprocessor.

### Status

The board runs both stacks. NuttX reaches an interactive NuttShell prompt when the console is on UART0, read
over the board's onboard CH343 bridge, and ESP-IDF v5.5.5 runs and prints over the same cable. The saved
configuration is `configs/nuttx/esp32p4-function-ev-board/nsh-rev1`.

Only the USB Serial/JTAG console is broken. The `usbconsole` configuration still stops while attaching the
console interrupt, and the same image with the console moved to UART0 boots to a prompt, which locates the
fault in that driver rather than in the port as a whole or in the silicon.

The 32 MB of PSRAM also works, at up to 200 MHz under ESP-IDF.

### Silicon Revision

The board is an ESP32-P4-Function-EV-Board v1.5.2 and the chip on it reports revision v1.3. Those are two
separate things, the printed circuit board and the die, and both numbers belong in a bug report.
NuttX supports v3.0 and above, and the stock configuration compiles a revision check that calls `PANIC()` on anything older.
Two options bypass it:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
```

The stock `usbconsole` configuration plus those two lines is saved at
`configs/nuttx/esp32p4-function-ev-board/usbconsole-rev1/defconfig`:

```shell
make nuttx-distclean
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/esp32p4-function-ev-board/usbconsole-rev1
make nuttx-build
```

With the check bypassed, every boot prints:

```
WARNING: NuttX supports ESP32-P4 chip revision > v3.0 (chip revision is v1.3).
Ignoring this error and continuing because `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` is set...
THIS MAY NOT WORK! DON'T USE THIS CHIP IN PRODUCTION!
```

### Serial Port

The board has two USB-C connectors, and both appear as `/dev/ttyACM0`, so the name alone does not say which one
is in use. Tell them apart by USB vendor:

```shell
udevadm info -q property -n /dev/ttyACM0 | grep ID_VENDOR_ID
```

`303a` is Espressif, meaning the connector wired to the chip's own USB Serial/JTAG controller. `1a86` is
QinHeng, meaning the onboard WCH CH343 bridge, which reports as `USB Single Serial` with product ID `55d3`.

The CH343 sits on UART0, which ESP-IDF confirms at boot:

```
I (163) cpu_start: GPIO 38 and 37 are used as console UART I/O pins
```

Those are the pins the notes below once said needed an external USB to UART adapter. The board already carries
one, so no extra hardware is required for a UART0 console.

Either connector flashes the chip, because the ROM download mode listens on UART0 as well as on USB
Serial/JTAG. Flash with `make nuttx-flash-esp PORT=/dev/ttyACM0`. A board with a CP2102 bridge appears as
`/dev/ttyUSB0` instead.

### Where the Boot Stops

With `CONFIG_DEBUG_FEATURES` and `CONFIG_ESPRESSIF_LOG_LEVEL_VERBOSE`, the last output is the point where
NuttX attaches the console interrupt:

```
ABCD
D (347) cpu_start: calling init function: 0x40001596   (init_disable_rtc_wdt)
V (352) intr_alloc: esp_intr_alloc_intrstatus (cpu 0): Args okay. Resulting flags 0x802
D (366) intr_alloc: Connected src 22 to int 1 (cpu 0)
```

Source 22 is `ETS_USB_SERIAL_JTAG_INTR_SOURCE`, the console itself. The enum in
`soc/esp32p4/include/soc/interrupts.h` needs care to decode, because it contains an alias
(`ETS_TEMPERATURE_SENSOR_INTR_SOURCE = ETS_LP_TSENS_INTR_SOURCE`) and a re-anchor (`ETS_LP_UART_INTR_SOURCE =
16`). Counting entries by hand gives the wrong answer. A correct decode maps the first allocation, source 53,
to `ETS_SYSTIMER_TARGET0_INTR_SOURCE`, the boot tick.

The `A`, `B`, `C`, and `D` markers come from `showprogress()` in `esp_start.c`, so `__esp_start()` completes
and `nx_start()` runs. Driver probes show the scheduler tick firing, the console interrupt service routine
running, and the serial layer toggling its transmit interrupt, so the kernel is alive when output stops.

### Factors That Do Not Affect the Hang

Each of the following is eliminated by a clean rebuild and a flash:

- Brownout detection, whether enabled or disabled.
- The inverted `is_edge` test at `esp_irq.c:597`, which reads
  `esprv_int_get_type(cpuint) == INTR_TYPE_LEVEL` where `INTR_TYPE_LEVEL` is 0 and the console attaches as
  `ESP_IRQ_TRIGGER_LEVEL`. The test still looks wrong, but correcting it changes nothing here.
- The ROM CLIC patch in `esp_rom_clic.c`. Stubbing out its `CLIC_INT_CTRL_REG` write makes no difference, and
  the offset in it is correct, since `esp_cpu_intr_set_type` passes the raw CPU interrupt number and the patch
  adds the CLIC base of 16.
- Disabling the RWDT and MWDT0 flashboot watchdogs at the top of `__esp_start()`.

### Workarounds to Avoid

Two edits to `external/nuttx` circulate for this board. Neither is needed, and one is harmful.

Adding `PROVIDE(esprv_intc_int_set_type = 0);` to
`boards/risc-v/esp32p4/common/scripts/esp32p4_aliases.ld` silences this link error:

```
riscv32-none-elf-ld.bfd: rom.api.ld.tmp:5: undefined symbol `esprv_intc_int_set_type' referenced in expression
```

It also resolves a live function to address zero. The map file shows the call site in `intr_alloc.o` binding
to it:

```
nuttx.map: 0x00000000  PROVIDE (esprv_int_set_type = esprv_intc_int_set_type)
nuttx.map: esprv_int_set_type   staging/libarch.a(intr_alloc.o)
nm nuttx:  00000000 A esprv_int_set_type
```

That link error is a symptom of a stale build rather than a binutils 2.46 problem. `esp_rom_clic.c` is guarded
by `#if ESP_ROM_CLIC_INT_TYPE_PATCH && CONFIG_ESP32P4_SELECTS_REV_LESS_V3`. Compiled while that option is off,
the object holds no symbols and the linker falls back to the `PROVIDE`. After a clean rebuild with the option
set, the object defines the function, the `PROVIDE` never fires, and the link succeeds:

```
nuttx.map: [!provide]  PROVIDE (esprv_int_set_type = esprv_intc_int_set_type)
nm nuttx:  40001252 T esprv_int_set_type
```

The second edit moves the RWDT and MWDT0 flashboot disable to the top of `__esp_start()` in
`arch/risc-v/src/common/espressif/esp_start.c`. On a correctly built tree it makes no difference: images with
and without it stop at the same point.

### Configuration Changes Require a Clean Rebuild

NuttX does not reliably recompile the vendored Espressif HAL objects when `.config` changes, which is how the
stale `esp_rom_clic.o` above arises. An image with an unchanged SHA-256 after a configuration change means the
build was skipped, not that it was unnecessary. Run `make nuttx-distclean`, configure again, and build from
scratch.

### The USB Console Cannot Report Failures

This applies to the `usbconsole` configuration only. With the console on UART0, panics and assertions print
normally, so the limitation below is a property of that driver rather than of the board.

In the `usbconsole` configuration all UARTs are disabled, so `CONSOLE_UART` is undefined. `riscv_lowputc()`
has a `#elif defined(CONFIG_ESPRESSIF_USBSERIAL)` branch that calls `esp_usbserial_write()`, so the path is
not empty, but that function spins without a bound:

```c
void esp_usbserial_write(char ch)
{
  while (!esp_txready(&g_uart_usbserial));

  esp_send(&g_uart_usbserial, ch);
}
```

It is reached from `up_putc`, so the first kernel print wedges the CPU whenever the USB endpoint is not
draining. With `CONFIG_DEBUG_*` enabled the board dies before the console is attached; bounding the loop lets
the boot proceed further. The bound is a candidate patch for upstream.

The consequence is that panics, assertions, and syslog produce no output in that configuration.

### PSRAM Works

The 32 MB of PSRAM works on this board at up to 200 MHz, tested under ESP-IDF v5.5.5 by
[../experiments/espidf-psram](../experiments/espidf-psram). It needs no settings beyond the two revision
options, because `CONFIG_SPIRAM=y` and the 200 MHz default are enough:

```
I (162) hex_psram: density      : 0x07 (256 Mbit)
I (167) hex_psram: good-die     : 0x06 (Pass)
I (184) hex_psram: BitMode      : 0x01 (X16 Mode)
I (198) MSPI Timing: Enter psram timing tuning
I (375) esp_psram: Found 32MB PSRAM device
I (375) esp_psram: Speed: 200MHz
I (1327) esp_psram: SPI SRAM memory test OK
I (1411) esp_psram: Adding pool of 32768K of PSRAM memory to heap allocator
```

The chip is an AP Memory generation 4 part, 256 Mbit, in X16 mode, reporting `good-die`. Both 20 MHz and
200 MHz pass, at 200 MHz after timing tuning, and a write-then-verify pattern over 1 MB passes at both. The
heap allocator gains 33551716 bytes.

250 MHz is not available on this silicon. Its option carries `depends on !ESP32P4_SELECTS_REV_LESS_V3`, so the
revision switch that makes the board boot at all also caps PSRAM at 200 MHz. That is the board's rated speed
regardless.

An ESPHome report against the same board and revision describes a load access fault at `0x5008e1a0`, inside
the PSRAM controller register space, whenever any PSRAM configuration was present, under ESP-IDF 5.5.4 across
20, 100, and 200 MHz. See https://github.com/esphome/esphome/issues/16903. That does not reproduce here.
Whatever the cause was, it was not this revision being incapable of driving its PSRAM.

An earlier version of these notes said PSRAM power runs through LDO channel 3 at 2.5 V, taken from that
report. That was wrong. ESP-IDF puts PSRAM on channel 2 at 1.8 V, declared `range 2 2` with 1800 mV as the
only voltage, and `ESP_LDO_RESERVE_PSRAM` defaults to enabled, so nothing needs configuring. Channel 3 at
2.5 V is the MIPI DSI PHY supply, which is a different peripheral.

### The Revision Needs an Explicit Switch Everywhere

Two independent projects require one for this silicon, `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` in NuttX and
`engineering_sample: true` in ESPHome, which is a fair measure of how much the revision matters.

### ESP-IDF Runs on This Board

ESP-IDF v5.5.5 is a submodule at `external/esp-idf`, with `espidf-` prefixed Makefile targets and a smallest
program at [../experiments/espidf-hello](../experiments/espidf-hello). It builds, flashes, and runs. The
console output over the CH343 bridge:

```
I (192) efuse_init: Min chip rev:     v1.0
I (196) efuse_init: Max chip rev:     v1.99
I (200) efuse_init: Chip rev:         v1.3
I (288) main_task: Calling app_main()
espidf-hello on chip revision v1.3, 2 cores
free heap: 600124 bytes
tick 0
```

Both cores come up, the scheduler runs, and 586 KB of heap is available without PSRAM, spread across
retention RAM, ordinary RAM, RTC RAM, and the SPM region. This settles the question the NuttX work could not:
the silicon boots to a working multicore system, so the NuttX hang is a port defect.

The revision options carry the same names as in NuttX, because NuttX took them from here:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
```

ESP-IDF defaults to `CONFIG_ESP32P4_REV_MIN_301`, and its bootloader rejects an image whose minimum revision
is above the chip's. The two options together move the accepted window to v1.0 through v1.99, which contains
v1.3. The Kconfig help states that revisions below v3.0 and revisions from v3.0 up have large hardware
differences and that support for the two is mutually exclusive, so the option is a fork in the hardware
abstraction layer rather than a relaxed check.

The console needs `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, since ESP-IDF puts the console on UART0 by default
and nothing is attached to GPIO37 and GPIO38 yet.

The compilers are prebuilt downloads rather than Nix packages, and `make espidf-install` places them under
`build/espressif` rather than in `~/.espressif`. That is about 2.6 GB. The compiler is
`riscv32-esp-elf-gcc 14.2.0` from release `esp-14.2.0_20260121`, which is the release CI installs for the
NuttX job on this board, so both paths use one compiler version.

### NuttShell over UART0

The stock `esp32p4-function-ev-board:nsh` configuration puts the console on UART0 at 115200 baud and enables no
USB serial driver. With the two revision options added, it boots to a prompt:

```
NuttShell (NSH)
nsh> uname -a
NuttX 0.0.0 2b5509e4-dirty Jan  1 1980 00:00:00 risc-v esp32p4-function-ev-board
```

```shell
make nuttx-distclean
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/esp32p4-function-ev-board/nsh-rev1
make nuttx-build
make nuttx-flash-esp PORT=/dev/ttyACM0
make console TTY=/dev/ttyACM0
```

The shell reports 217 KB of free heap and holds two tasks, the idle task and `nsh_main`. `/dev` carries
`console`, `null`, `random`, `ttyS0`, and `zero`. There is no filesystem beyond that yet, and no radio, since
the wireless parts belong to the onboard ESP32-C6.

ESP-IDF reports 586 KB of free heap on the same silicon, against 217 KB here. The difference is configuration
rather than hardware, since this image is a plain `nsh` with no PSRAM and a small heap region.

### A Filesystem and Shell Tools

`configs/nuttx/esp32p4-function-ev-board/nsh-tools-rev1` adds littlefs on the internal flash, `vi`, `nxdiag`,
tab completion, and command history to `nsh-rev1`. None of it needs a patch, since
`ESPRESSIF_SPIFLASH_LITTLEFS` and its automatic bring-up are already in the board tree:

```
CONFIG_ESPRESSIF_SPIFLASH=y
CONFIG_ESPRESSIF_SPIFLASH_AUTO_BRINGUP=y
CONFIG_ESPRESSIF_SPIFLASH_LITTLEFS=y
CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET=0x110000
CONFIG_ESPRESSIF_STORAGE_MTD_SIZE=0xf0000
```

The image grows from 274676 to 323360 bytes. The filesystem mounts itself at `/data` with 960 KB, and files
written there survive a reboot:

```
nsh> mount
  /data type littlefs
  /proc type procfs
nsh> df -h
  Filesystem      Size      Used  Available Mounted on
  littlefs        960K        8K       952K /data
nsh> cat /data/hello.txt
p4-persistence-test
```

The flash layout leaves the application at 0x002000, running to about 0x050F60, and the storage partition from
0x110000 to 0x200000. The 764 KB gap between them comes from the stock `spiflash` configuration and is
harmless. The partition ends at 2 MB, which is what the image header declares, even though the chip carries
32 MB.

### PSRAM Under NuttX

NuttX drives the same PSRAM through `CONFIG_ESPRESSIF_SPIRAM`, and it works. The stock `psram_usrheap`
configuration plus the two revision options is saved as
`configs/nuttx/esp32p4-function-ev-board/psram-usrheap-rev1`. It also defaults to 200 MHz and runs its own
boot-time memory test.

PSRAM becomes the user heap while internal RAM stays as the kernel heap, which `free` reports as two regions:

```
nsh> free
      total       used       free    maxused    maxfree  nused  nfree name
     471532       6580     464952       8680     262128     38      3 Kmem
   33554428       4276   33550152     842496   33550152      8      1 Umem
```

`Umem` is the full 32 MB. The configuration also builds the `heap` test app, which allocates and frees through
that pool at addresses in the `0x4800xxxx` range. It completes without reporting a failure, and the `maxused`
figure of 842496 above is what it peaked at before returning everything, so the pool takes allocation and
release cleanly rather than merely reporting a size.

This configuration also enables the SPI flash with SmartFS, which is unformatted on a fresh board, so the boot
prints `Failed to mount the FS volume: -19` and suggests `mksmartfs /dev/smart0`. That is unrelated to PSRAM
and is fixed by running that command once.

### The Comprehensive Configuration

`configs/nuttx/esp32p4-function-ev-board/nsh-full-rev1` combines the PSRAM heap with the shell tools and every
on-chip peripheral that needs no wiring. It is 524316 bytes, which is 47 percent of the space between the
application start at 0x002000 and the filesystem at 0x110000.

Three filesystems mount themselves at boot:

```
nsh> df -h
  Filesystem      Size      Used  Available Mounted on
  littlefs        960K        8K       952K /data
  procfs            0B        0B         0B /proc
  tmpfs           512B      512B         0B /tmp
```

`/data` is persistent across reboots and reflashes. Memory is 32 MB of PSRAM as the user heap with internal RAM
as the kernel heap, so `top` reports about 34 MB total.

Device nodes: `adc0`, `efuse`, `espflash`, `gpio0` through `gpio3`, `i2c0`, `leds0`, `lirc0`, `lirc1`,
`oneshot`, `pwm0`, `random`, `urandom`, `rtc0`, `spi2`, `timer0`, `watchdog0`, `watchdog1`, and
`uorb/sensor_temp0`.

Builtin apps: `adc`, `alarm`, `dd`, `dumpstack`, `getprime`, `gpio`, `heap`, `i2c`, `nxdiag`, `oneshot`,
`ostest`, `pwm`, `rand`, `sensortest`, `spi`, `timer`, `vi`, `wdog`, and `ws2812`. The shell has tab
completion, command history, pipelines, and `top`.

Two things work with no wiring at all. The on-chip temperature sensor reads through uORB:

```
nsh> sensortest -n 3 temp0
temp0: timestamp:71850000 value:32.00
```

And `top` reports per-task CPU load, which needs `CONFIG_SCHED_CPULOAD_SYSCLK`.

### The RTC Does Not Survive a Reset

`date` works within a session. Setting the clock and reading it back is correct:

```
nsh> date -s "Jan 01 00:00:00 2026"
nsh> date
Thu, Jan 01 00:00:09 2026
```

After a reboot it returns a nonsense year, and a different one each time. Three consecutive reboots from the
same set time gave 502155, 420154, and 335426. The value is not a fixed multiple of what was set, so this looks
like the driver reading an uninitialised or wrongly scaled counter after reset rather than a units mistake in
one direction.

`CONFIG_START_YEAR` and its companions do not help, because with `CONFIG_RTC_DRIVER=y` the time comes from the
hardware counter rather than from those build-time values. Treat the clock as something to set after each boot,
and do not rely on it across a reset.

### Boot Messages That Are Not Faults

The ROM prints a hash mismatch on every boot and continues:

```
SHA-256 comparison failed:
Calculated: 3e61d9f3631351f0b62cfedd01e5b96623047f31f2f7ecd12abc1453fcf07910
Expected: 00000000d0260000000000000000000000000000000000000000000000000000
Attempting to boot anyway...
```

The expected field is not populated by this NuttX build, so the check has nothing valid to compare against. The
image runs correctly.

The three-line chip revision warning is the bypassed `PANIC()` announcing itself and is also expected.

### The Whole 32 MB, and Filesystem Throughput

`configs/nuttx/esp32p4-function-ev-board/nsh-full-32m-rev1` uses the whole flash chip and tunes littlefs for
throughput. It differs from `nsh-full-rev1` in six options:

```
CONFIG_ESPRESSIF_FLASH_32M=y
CONFIG_ESPRESSIF_STORAGE_MTD_SIZE=0x1ef0000
CONFIG_FS_LITTLEFS_BLOCK_SIZE_FACTOR=16
CONFIG_FS_LITTLEFS_CACHE_SIZE_FACTOR=16
CONFIG_TESTING_FSTEST=y
CONFIG_TESTING_FSTEST_MAXFILE=65536
```

The stock configuration declares 4 MB of flash and a 960 KB storage partition, so most of the chip is
unreachable. Declaring 32 MB and extending the partition to `0x1ef0000` gives a 30 MB filesystem, which
`fstest` passes with no failures, and which littlefs confirms independently by reporting 7920 blocks of
4096 bytes, exactly the configured size.

Measured with `dd` over 4 MB:

| Configuration | Write | Read |
| --- | --- | --- |
| 1 KB cache, 4 KB blocks | 132 KB/s | 2884 KB/s |
| 4 KB cache, 4 KB blocks | 131 KB/s | 3976 KB/s |
| 4 KB cache, 64 KB blocks | 222 KB/s | 6206 KB/s |

Writes are limited by flash erase time rather than by software. At 4 KB blocks, 4 MB took 31.19 seconds for
1024 blocks, which is 30 ms each and is ordinary sector erase latency, so the cache made no difference to
writes at all. It did improve reads by 38 percent, and costs only RAM, since cache size is a runtime parameter
rather than part of the on-disk format.

Raising the block size to 64 KB is the change that moves writes, because one 64 KB block erase replaces sixteen
4 KB sector erases. There is no point going further, since SPI NOR has no larger erase primitive short of
erasing the chip.

The cost is granularity. 64 KB blocks give 495 blocks rather than 7920, and a non-inlined file occupies at
least one block, so a 1 KB file can consume 64 KB. That suits a 30 MB volume holding large files and suits
`nsh-full-rev1` and its 960 KB volume much less, which is why the smaller configuration keeps 4 KB blocks.

Changing the block size changes the on-disk format, so `/data` reformats on the first boot after the switch.

### Wireless Needs the C6, and the C6 Needs New Firmware

The P4 has no radio, so Wi-Fi and Bluetooth mean driving the onboard ESP32-C6 over SDIO. Under NuttX there is no
path at all: the `esp32p4-function-ev-board` port has no SDIO host driver, no Kconfig, and no configuration for
the companion, and the only P4 board in the tree that mentions it is `esp32p4-tab5`, as a pin map.

Under ESP-IDF the path exists and is the vendor's own. Pair two managed components, `esp_wifi_remote` for the
API and `esp_hosted` for the transport, with `CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED=y`.

Do not use `CONFIG_ESP_HOST_WIFI_ENABLED`, despite it looking like the obvious switch and despite the P4 being
the only target that declares the `SOC_WIRELESS_HOST_SUPPORTED` capability it depends on. The component's own
Kconfig reads:

```
menuconfig ESP_HOSTED
	default n if ESP_HOST_WIFI_ENABLED
	default y if ESP_WIFI_REMOTE_ENABLED && ESP_WIFI_REMOTE_LIBRARY_HOSTED
```

So enabling it switches ESP-Hosted off. The two are mutually exclusive, and the in-tree one has
`ESP_WIFI_CONTROLLER_TARGET="esp32"` rather than a C6. It builds cleanly and produces an image that can never
reach the radio.

With the correct pairing the host side works. The transport picks the right pins unaided, matching Espressif's
documentation for this board, and then the C6 fails to enumerate as an SDIO card. See
[../experiments/espidf-wifi](../experiments/espidf-wifi) for the log and for what was ruled out: both 40 and
20 MHz clocks, and both the 3.0.6 and 2.7.4 host generations.

The C6 ships with slave firmware v0.0.6, which Espressif recommends upgrading. Reflashing it needs a 3.3 V USB
to UART adapter on the `PROG_C6` header, wiring `ESP_EN`, `ESP_TXD`, `ESP_RXD`, and `GND`, and not VDD, with the
P4 held in its bootloader. The over-the-air route needs a working link and so cannot bootstrap from here.

### Next Step

Flash the C6 with current slave firmware once a USB to UART adapter is available. That is the one blocker
between this board and working Wi-Fi and Bluetooth.

Three defects are worth reporting upstream, all in the NuttX Espressif port rather than in the silicon: the
unbounded spin in `esp_usbserial_write()`, the boot stopping while attaching interrupt source 22 with the USB
console, and the RTC returning nonsense after a reset.
