## ESP32-P4 Notes

Notes on bringing up Apache NuttX on the ESP32-P4 function EV board, with 32 MB of PSRAM, headers, and an
onboard ESP32-C6 wireless coprocessor.

### Status

The board boots NuttX and prints through the boot sequence. It does not reach a NuttShell prompt, so it is not usable yet.
The Raspberry Pi Pico 2 is the board that works over USB alone.
See [raspberrypi-pico-2.md](raspberrypi-pico-2.md).

### Silicon Revision

This sample reports revision v1.3.
NuttX supports v3.0 and above, and the stock configuration compiles a revision check that calls `PANIC()` on anything older.
Two options bypass it are:

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

The USB-C connector goes to the chip's own USB Serial/JTAG controller, so the board appears as `/dev/ttyACM0`,
not `/dev/ttyUSB0`. Flash it with `make nuttx-flash-esp PORT=/dev/ttyACM0`. A board with a CP2102 bridge
appears as `/dev/ttyUSB0` instead.

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

### Next Step

Move the console to UART0, on GPIO37 for transmit and GPIO38 for receive, and read it with a USB to UART
adapter. That restores a working `up_putc`, making panics and assertions visible, and removes the USB
Serial/JTAG driver from the picture. The adapter must drive 3.3V logic, since a 5V-only one can damage the
pins. The stock `esp32p4-function-ev-board:nsh` configuration puts the console on UART0.
