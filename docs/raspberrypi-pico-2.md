## Raspberry Pi Pico 2 Notes

Apache NuttX on the Raspberry Pi Pico 2 WH, an RP2350 board with headers and a wireless module.

### Status

Working. The board runs a NuttShell over the USB cable, with no extra hardware.

```
nsh> uname -a
NuttX 0.0.0 2b5509e4 Jan  1 1980 00:00:00 arm raspberrypi-pico-2
nsh> free
      total       used       free    maxused    maxfree  nused  nfree name
     517456       9720     507736      10088     507736     43      1 Umem
```

### Board Name

The NuttX board is `raspberrypi-pico-2`. There is no separate `-w` variant in the tree, and that board
configuration runs correctly on the WH, with the wireless module unused. The CYW43439 driver does exist, at
`arch/arm/src/rp23xx/rp23xx_cyw43439.c`, but the board support that wires it up lives in
`boards/arm/rp23xx/pimoroni-pico-plus-2-w`, so Wi-Fi on this board needs work borrowed from there.

### Build, Flash, and Connect

```shell
make nuttx-configure BOARD=raspberrypi-pico-2:usbnsh
make nuttx-build
make flash-pico-uf2
make console TTY=/dev/ttyACM0
```

The `usbnsh` configuration puts the shell on the USB cable through `CONFIG_CDCACM_CONSOLE`. The `nsh`
configuration sets `CONFIG_UART0_SERIAL_CONSOLE` instead and brings up no USB device at all, so
`/dev/ttyACM0` never appears with it. Pick `usbnsh` unless a USB to UART adapter is wired to GP0 and GP1.

### Press Enter Twice

The first Enter on a fresh connection only echoes. The session starts on the second one:

```
enter 1: b'\r\n'
enter 2: b'\r\n\r\nNuttShell (NSH)\r\nnsh> '
```

A bare echo does not indicate a working shell. NuttX echoes received characters in the serial driver when
`ECHO` is set, so keystrokes come back even when nothing is reading them. The prompt is the only reliable
sign that a session is live.

### Flashing Always Needs the BOOTSEL Button

Every flash needs the board physically placed in BOOTSEL mode: unplug it, hold BOOTSEL, plug it back in, and
release. There is no software path. NuttX exposes no picotool reset interface, so `picotool reboot -f -u`
answers `No accessible RP-series devices in BOOTSEL mode were found` while the board is running. Each flash
therefore costs a physical button press.

Two ways to flash once the board is in BOOTSEL:

- `make flash-pico-uf2` copies `nuttx.uf2` to the mass storage volume the board exposes, labelled `RP2350`. It
  needs no special permissions and no setup.
- `make flash-pico` uses `picotool load -fx`, which verifies what it writes. It needs the udev rules that
  `make setup-udev` installs once, and without them it fails with `Maybe try 'sudo' or check your
  permissions`.

### Saved Configurations

- `configs/nuttx/raspberrypi-pico-2/usbnsh-tools/defconfig` is the stock `usbnsh` configuration plus
  `CONFIG_SYSTEM_VI` and `CONFIG_SYSTEM_NXDIAG`, which add `vi` and `nxdiag` as builtin applications. The two
  apps cost about 17.6 KB of flash and no RAM at all, since `data` and `bss` are unchanged.
- `configs/nuttx/raspberrypi-pico-2/usbnsh-littlefs/defconfig` is `usbnsh-tools` plus `CONFIG_MTD`,
  `CONFIG_RP23XX_FLASH_MTD`, and `CONFIG_FS_LITTLEFS` for persistent storage on the internal flash, and
  `CONFIG_READLINE_TABCOMPLETION` and `CONFIG_READLINE_EDIT_EMACS` for shell editing.

Rebuild either one with:

```shell
make nuttx-distclean
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/raspberrypi-pico-2/usbnsh-tools
make nuttx-build
```

Two clean builds of the same configuration are not byte-identical. The `text` size can differ by a few dozen
bytes with `data` and `bss` unchanged, which reflects link ordering rather than a difference in content.

### Memory and Storage

The board carries 520 KB of on-chip SRAM, as 512 KB in the main banks plus two 4 KB scratch banks, and a
separate 4 MB QSPI flash chip. There is no PSRAM. The RP2350 can address PSRAM over its second chip select,
and `CONFIG_RP23XX_PSRAM` is wired into `rp23xx_boardinitialize.c`, but the Pico 2 leaves that footprint
unpopulated, so the option does nothing here.

NuttX reports 517456 bytes of heap, of which about 507736 are free, and a 355 KB image leaves roughly 3.6 MB
of the flash unused.

The `usbnsh` configuration mounts nothing but procfs. `CONFIG_FS_PROCFS` and `CONFIG_FS_ANONMAP` are the only
filesystems compiled in, so there is no writable storage at all. Three ways to change that, cheapest first:

- `littlefs` on the internal flash is the persistent option, and it needs no extra hardware. See the section
  below.
- `tmpfs` gives a writable directory in RAM, at the cost of losing it on every reset. One configuration
  option, and there is plenty of heap for it.
- FAT on an SD card, through the `spisd` board configuration, needs hardware the board does not have.

### littlefs on the Internal Flash

`CONFIG_MTD`, `CONFIG_RP23XX_FLASH_MTD`, and `CONFIG_FS_LITTLEFS` are enough, and no board code is needed.
`rp23xx_common_bringup.c` registers the region as `/dev/rpflash` on its own. The defaults place it 1 MB into
flash and give it 1 MB, which clears the image with room to spare:

```
CONFIG_RP23XX_FLASH_MTD_OFFSET=0x100000
CONFIG_RP23XX_FLASH_MTD_SIZE=0x100000
```

The driver refuses to initialize if the region would overlap `__flash_binary_end`, so the offset has to stay
beyond the end of the image, on a 4096 byte erase sector boundary.

Nothing is mounted automatically, apart from XIPFS when `CONFIG_FS_XIPFS` is set. Format and mount on the
first boot, then mount without the option afterwards:

```
mount -t littlefs -o autoformat /dev/rpflash /mnt
mount -t littlefs /dev/rpflash /mnt
```

`df` then reports 256 blocks of 4096 bytes. Files written to `/mnt` survive `reboot`, a power cycle, and
reflashing the firmware, because the region starts beyond the image. An image that grew past the 1 MB offset
would collide with it, and the driver refuses to initialize in that case rather than corrupting the data.

### Shell Editing and History

The `usbnsh-littlefs` configuration enables these, and they are verified on the board:

- Command history on the up and down arrows, holding 16 lines of up to 80 characters, from
  `CONFIG_READLINE_CMD_HISTORY`. The buffer lives in RAM and does not survive a reset, and there is no
  `history` command to list it.
- Tab completion of command names, from `CONFIG_READLINE_TABCOMPLETION`. An ambiguous prefix lists the
  candidates, so `una` and Tab prints `unalias` and `uname`, while `up` and Tab completes to `uptime`.
- Emacs style line editing with Ctrl-R reverse search, from `CONFIG_READLINE_EDIT_EMACS` and
  `CONFIG_READLINE_EDIT_EMACS_REVERSE_SEARCH`.

There is no `clear` command in NSH, and Ctrl-L does nothing. Clearing the display is the terminal emulator's
job on the host side.

### There Is No SD Card Slot

The board has no card socket. The `spisd` configuration drives an external microSD breakout over SPI0, so it
needs a module and wiring before it does anything. It sets `CONFIG_MMCSD`, `CONFIG_FS_FAT`, and
`CONFIG_RP23XX_SPISD`, and expects these pins:

| Signal    | GPIO |
| --------- | ---- |
| SCK       | GP2  |
| TX (MOSI) | GP3  |
| RX (MISO) | GP4  |
| CS        | GP5  |

The module also needs 3V3 and ground.

### Adding More Tools

NuttX has its own versions of some familiar commands, in `external/nuttx-apps`. `system/vi`, `system/curl`,
`system/tcpdump`, `system/cpuload`, `system/stackmonitor`, `netutils/webclient`, and `netutils/dropbear` are
all there, along with `ping`, `telnet`, and `netcat`. There is no `grep`, `find`, `sed`, or `awk` anywhere in
the tree.

Anything that needs a network is blocked until the CYW43439 is wired up for this board.

`system/cpuload` and `system/stackmonitor` need kernel options rather than plain application options:
`SCHED_CPULOAD_SYSCLK` in place of `SCHED_CPULOAD_NONE`, and `STACK_COLORATION`. Neither is verified on this
board. Kernel options carry more risk than application options, so enable them one at a time.
