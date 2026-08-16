# Raspberry Pi Pico 2 Notes

Notes on running Apache NuttX on the Raspberry Pi Pico 2 WH, an RP2350 board with headers and a wireless
module.

## Status

Working. The board runs a NuttShell over the USB cable, with no extra hardware. This is the board to reach for
when something needs to be tried on real silicon.

```
nsh> uname -a
NuttX 0.0.0 2b5509e4 Jan  1 1980 00:00:00 arm raspberrypi-pico-2
nsh> free
      total       used       free    maxused    maxfree  nused  nfree name
     517456       9720     507736      10088     507736     43      1 Umem
```

## Board Name

The NuttX board is `raspberrypi-pico-2`. There is no separate `-w` variant in the tree, and that board
configuration runs correctly on the WH, with the wireless module unused. The CYW43439 driver does exist, at
`arch/arm/src/rp23xx/rp23xx_cyw43439.c`, but the board support that wires it up lives in
`boards/arm/rp23xx/pimoroni-pico-plus-2-w`, so Wi-Fi on this board needs work borrowed from there.

## Build, Flash, and Connect

```shell
make nuttx-configure BOARD=raspberrypi-pico-2:usbnsh
make nuttx-build
make flash-pico-uf2
make console TTY=/dev/ttyACM0
```

The `usbnsh` configuration puts the shell on the USB cable through `CONFIG_CDCACM_CONSOLE`. The `nsh`
configuration sets `CONFIG_UART0_SERIAL_CONSOLE` instead and brings up no USB device at all, so
`/dev/ttyACM0` never appears with it. Pick `usbnsh` unless a USB to UART adapter is wired to GP0 and GP1.

## Press Enter Twice

The first Enter on a fresh connection only echoes. The session starts on the second one:

```
enter 1: b'\r\n'
enter 2: b'\r\n\r\nNuttShell (NSH)\r\nnsh> '
```

A bare echo is not a sign that the board is broken. NuttX echoes received characters in the serial driver when
`ECHO` is set, so keystrokes come back even when no shell is reading them. Diagnosing this board by whether it
echoes leads nowhere; send a second Enter and look for the prompt.

## Flashing Always Needs the BOOTSEL Button

Every flash needs the board physically placed in BOOTSEL mode: unplug it, hold BOOTSEL, plug it back in, and
release. There is no software path. NuttX exposes no picotool reset interface, so `picotool reboot -f -u`
answers `No accessible RP-series devices in BOOTSEL mode were found` while the board is running. Plan work
around that, because each test on this board costs a button press.

Two ways to flash once the board is in BOOTSEL:

- `make flash-pico-uf2` copies `nuttx.uf2` to the mass storage volume the board exposes, labelled `RP2350`. It
  needs no special permissions and no setup.
- `make flash-pico` uses `picotool load -fx`, which verifies what it writes. It needs the udev rules that
  `make setup-udev` installs once, and without them it fails with `Maybe try 'sudo' or check your
  permissions`.

## Saved Configurations

- `configs/nuttx/raspberrypi-pico-2/usbnsh-tools/defconfig` is the stock `usbnsh` configuration plus
  `CONFIG_SYSTEM_VI` and `CONFIG_SYSTEM_NXDIAG`, which add `vi` and `nxdiag` as builtin applications. The two
  apps cost about 17.6 KB of flash and no RAM at all, since `data` and `bss` are unchanged.

Rebuild either one with:

```shell
make nuttx-distclean
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/raspberrypi-pico-2/usbnsh-tools
make nuttx-build
```

A clean rebuild from a saved configuration is not byte-identical to an earlier image of the same
configuration. The `text` size moved by 24 bytes between two such builds, with `data` and `bss` unchanged,
which is link ordering rather than a real difference.

## Adding More Tools

NuttX has its own versions of some familiar commands, in `external/nuttx-apps`. `system/vi`, `system/curl`,
`system/tcpdump`, `system/cpuload`, `system/stackmonitor`, `netutils/webclient`, and `netutils/dropbear` are
all there, along with `ping`, `telnet`, and `netcat`. There is no `grep`, `find`, `sed`, or `awk` anywhere in
the tree.

Anything that needs a network is blocked until the CYW43439 is wired up for this board.

`system/cpuload` and `system/stackmonitor` need kernel options rather than plain application options:
`SCHED_CPULOAD_SYSCLK` in place of `SCHED_CPULOAD_NONE`, and `STACK_COLORATION`. Neither has been tested on
this board. Add kernel options one at a time, since each attempt costs a BOOTSEL press.
