## ESP32-P4 Notes

Notes on bringing up Apache NuttX on the ESP32-P4 function EV board, with 32 MB of PSRAM, headers, and an
onboard ESP32-C6 wireless coprocessor.

### Status

The board boots NuttX and prints through the boot sequence, but it never reaches a NuttShell prompt.
It is not usable yet. The Raspberry Pi Pico 2 is the board that currently works over USB alone.
See [raspberrypi-pico-2.md](raspberrypi-pico-2.md).

### Silicon Revision

This sample reports revision v1.3, and NuttX supports v3.0 and above. The stock configuration compiles a
revision check that calls `PANIC()` on anything older, so it has to be told to continue:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
```

That combination is saved in this repository at
`configs/nuttx/esp32p4-function-ev-board/usbconsole-rev1/defconfig`, which is the stock `usbconsole`
configuration plus those two lines. Rebuild it with:

```shell
make nuttx-distclean
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/esp32p4-function-ev-board/usbconsole-rev1
make nuttx-build
```

With the check bypassed, the board prints a warning on every boot and carries on:

```
WARNING: NuttX supports ESP32-P4 chip revision > v3.0 (chip revision is v1.3).
Ignoring this error and continuing because `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` is set...
THIS MAY NOT WORK! DON'T USE THIS CHIP IN PRODUCTION!
```

### Serial Port

The USB-C connector goes straight to the chip's own USB Serial/JTAG controller, so the board appears as
`/dev/ttyACM0`, not `/dev/ttyUSB0`. Flash it with `make nuttx-flash-esp PORT=/dev/ttyACM0`. A board with a
CP2102 bridge would appear as `/dev/ttyUSB0` instead.

### Where It Stops

With `CONFIG_DEBUG_FEATURES` and `CONFIG_ESPRESSIF_LOG_LEVEL_VERBOSE` enabled, the last thing the board ever
prints is the moment NuttX attaches the console interrupt:

```
ABCD
D (347) cpu_start: calling init function: 0x40001596   (init_disable_rtc_wdt)
V (352) intr_alloc: esp_intr_alloc_intrstatus (cpu 0): Args okay. Resulting flags 0x802
D (366) intr_alloc: Connected src 22 to int 1 (cpu 0)
```

Source 22 is `ETS_USB_SERIAL_JTAG_INTR_SOURCE`, the console itself. That number has to be decoded with care,
because the enum in `soc/esp32p4/include/soc/interrupts.h` contains an alias
(`ETS_TEMPERATURE_SENSOR_INTR_SOURCE = ETS_LP_TSENS_INTR_SOURCE`) and a re-anchor (`ETS_LP_UART_INTR_SOURCE =
16`), so counting entries by hand gives the wrong answer. Decoding the enum properly also maps the first
allocation, source 53, to `ETS_SYSTIMER_TARGET0_INTR_SOURCE`, which is the expected boot tick and confirms the decode.

The `A`, `B`, `C`, and `D` markers come from `showprogress()` in `esp_start.c`, so `__esp_start()` completes
and `nx_start()` runs. Probes placed in the driver show the scheduler tick firing repeatedly, the console
interrupt service routine running, and the serial layer toggling its transmit interrupt, so the kernel is
alive when the output stops.

### What Has Been Ruled Out

Each of these was tested on the board with a clean rebuild and a flash. None of them changed the hang:

- Disabling brownout detection.
- Flipping the inverted `is_edge` test at `esp_irq.c:597`, which reads
  `esprv_int_get_type(cpuint) == INTR_TYPE_LEVEL` where `INTR_TYPE_LEVEL` is 0 and the console attaches as
  `ESP_IRQ_TRIGGER_LEVEL`. It still looks wrong, but it is not this bug.
- Stubbing out the ROM CLIC patch in `esp_rom_clic.c`, so the `CLIC_INT_CTRL_REG` write never happens. The
  offset in that patch is correct: `esp_cpu_intr_set_type` passes the raw CPU interrupt number and the patch
  adds the CLIC base of 16.
- Disabling the console watchdog patch described below.

### Two Workarounds That Should Not Be Used

An earlier round of debugging produced two edits inside `external/nuttx`. Both have been reverted, and neither
is needed.

The first added `PROVIDE(esprv_intc_int_set_type = 0);` to
`boards/risc-v/esp32p4/common/scripts/esp32p4_aliases.ld` to silence this link error:

```
riscv32-none-elf-ld.bfd: rom.api.ld.tmp:5: undefined symbol `esprv_intc_int_set_type' referenced in expression
```

That workaround is harmful. It resolved a real function to address zero, and the map file shows the call site
in `intr_alloc.o` binding to it:

```
nuttx.map: 0x00000000  PROVIDE (esprv_int_set_type = esprv_intc_int_set_type)
nuttx.map: esprv_int_set_type   staging/libarch.a(intr_alloc.o)
nm nuttx:  00000000 A esprv_int_set_type
```

The link error itself was not a binutils problem. It came from a stale build. `esp_rom_clic.c` is guarded by
`#if ESP_ROM_CLIC_INT_TYPE_PATCH && CONFIG_ESP32P4_SELECTS_REV_LESS_V3`, and it had been compiled while that
option was still off, so the object held no symbols and the linker fell back to the `PROVIDE`. After a clean
rebuild with the option set, the object defines the function, the `PROVIDE` never fires, and the link succeeds
with binutils 2.46:

```
nuttx.map: [!provide]  PROVIDE (esprv_int_set_type = esprv_intc_int_set_type)
nm nuttx:  40001252 T esprv_int_set_type
```

The second edit moved the RWDT and MWDT0 flashboot disable to the top of `__esp_start()` in
`arch/risc-v/src/common/espressif/esp_start.c`. On a correctly built tree it makes no difference. Images built
with and without it hang at exactly the same place.

### Always Rebuild Clean After a Configuration Change

NuttX does not reliably recompile the vendored Espressif HAL objects when `.config` changes. That is what
created the stale `esp_rom_clic.o` above, and it happened twice more during this work: after enabling
`CONFIG_DEBUG_*` and rebuilding, the flashed image had a byte-identical SHA-256, which means nothing was
recompiled at all. When an image does not change after a configuration change, the build was skipped, not unnecessary.
Run `make nuttx-distclean`, configure again, and build from scratch.

### The Board Cannot Report Its Own Failures

In the `usbconsole` configuration all UARTs are disabled, so `CONSOLE_UART` is undefined. `riscv_lowputc()`
does have a `#elif defined(CONFIG_ESPRESSIF_USBSERIAL)` branch that calls `esp_usbserial_write()`, so the path
is not empty, but that function spins forever:

```c
void esp_usbserial_write(char ch)
{
  while (!esp_txready(&g_uart_usbserial));

  esp_send(&g_uart_usbserial, ch);
}
```

It is reached from `up_putc`, so the first kernel print wedges the CPU whenever the USB endpoint is not
draining. With `CONFIG_DEBUG_*` enabled the board died before the console was even attached, and bounding that
loop let the boot proceed further. A candidate patch for that is worth reporting upstream.

The practical consequence is that panics, assertions, and syslog are invisible on this board. Every conclusion
here was drawn through that blind spot.

### Next Step

Move the console to UART0, on GPIO37 for transmit and GPIO38 for receive, and read it with a USB to UART
adapter. That gives a real `up_putc`, so panics and assertions become visible, and it removes the suspect
driver from the picture. The adapter has to drive 3.3V logic, because a 5V-only one can damage the pins. Build
the stock `esp32p4-function-ev-board:nsh` configuration for that, since it puts the console on UART0.
