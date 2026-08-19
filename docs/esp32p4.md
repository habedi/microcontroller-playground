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

### The Board Cannot Report Its Own Failures

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

The consequence is that panics, assertions, and syslog produce no output on this board.

### PSRAM Is Broken on This Revision

Enabling PSRAM crashes this board, and not only under NuttX. An ESPHome report against the same
ESP32-P4-Function-EV-Board v1.5.2 with the same chip revision v1.3 and the same
`ESP-ROM:esp32p4-eco2-20240710` describes a load access fault at `0x5008e1a0`, inside the PSRAM controller
register space, whenever any PSRAM configuration is present. That is with ESP-IDF 5.5.4, across 20, 100, and
200 MHz settings, and with the pre-production silicon flag already set. See
https://github.com/esphome/esphome/issues/16903.

Neither the stock `usbconsole` configuration nor the saved `usbconsole-rev1` enables PSRAM, so this is not the
cause of the hang described above. It matters for two other reasons.

The board's 32 MB of PSRAM is unusable for now, whatever the software. Anything wanting large buffers, such as
a framebuffer or an audio pipeline, has to fit in internal RAM instead.

Powering PSRAM on this board goes through an internal LDO, which the ESPHome configuration sets as channel 3
at 2.5 V. A port that omits that setup would fail in much this way, so the LDO is the first thing to check if
PSRAM is ever attempted here.

### ESP-IDF Reportedly Works on This Board

The same report states that without PSRAM the board "boots and runs perfectly including WiFi, HA API, I2C, and
touch detection" under ESP-IDF. That is the vendor stack reaching a working system on this exact board and
revision, while the NuttX build here cannot reach a shell.

The balance of evidence therefore points at the NuttX port rather than the silicon, and it makes ESP-IDF the
higher-probability route to a usable board.

Two independent projects need an explicit switch for this silicon, `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` in
NuttX and `engineering_sample: true` in ESPHome, which is a fair measure of how much the revision matters.

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

### Next Step

Add a filesystem and the tools that make the shell useful, following what the Pico 2 already has. The saved
`nsh-rev1` configuration is the base to build on.

The USB Serial/JTAG console remains broken and is worth reporting upstream. The unbounded spin in
`esp_usbserial_write()` is one defect, and the boot stopping while attaching interrupt source 22 is the other.
