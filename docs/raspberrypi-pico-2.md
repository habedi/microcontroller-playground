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
configuration runs correctly on the WH. Upstream does not wire up the wireless module for it; the patch
described under Wireless below does, borrowing from `boards/arm/rp23xx/pimoroni-pico-plus-2-w`.

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
- `configs/nuttx/raspberrypi-pico-2/usbnsh-wifi/defconfig` is `usbnsh-littlefs` plus the CYW43439 driver, the
  network stack, and the `wapi`, `ping`, and `renew` tools. It needs the patch and the firmware described
  under Wireless below.

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

### Wireless

Scanning works. Bluetooth does not, because NuttX has no CYW43439 Bluetooth driver at all, only the Wi-Fi one
at `arch/arm/src/rp23xx/rp23xx_cyw43439.c`.

Three pieces are needed, none of which upstream provides for this board:

1. `patches/nuttx/0001-raspberrypi-pico-2-cyw43439-wireless.patch` adds the four `GPIO_CYW43439_*` pins, the
   `rp23xx_cyw_setup()` call in `rp23xx_bringup.c`, `include/rp23xx_extra_gpio.h`, and the firmware staging
   rules. The pins are 23 for power, 24 for data, 25 for chip select, and 29 for the clock, the same as every
   Pico W. `nuttx-configure` and `nuttx-configure-saved` apply it, so it is hard to forget. `make nuttx-patch`
   applies it on its own and is idempotent, and `make nuttx-unpatch` removes it.
2. The firmware blob. `make cyw43-firmware` converts the C array in
   `external/pico-sdk/lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h` into the 225240 byte binary
   NuttX expects, at `build/cyw43439-firmware.bin`. The configuration finds it through `PLAYGROUND_ROOT`,
   which the dev shell exports, so no saved defconfig carries a machine specific path and the pico-sdk
   submodule stays clean.
3. The `cyw43-driver` submodule, which is nested inside pico-sdk. `make submodules` fetches it.

Bring the interface up by hand, because the saved configuration leaves `CONFIG_NSH_NETINIT` off so that a
wireless problem cannot cost the shell:

```
ifup wlan0
wapi scan wlan0
```

`ifup` is what downloads the firmware to the chip, which takes a moment. After it, `ifconfig` reports a real
MAC address read from the chip rather than all zeros, and `wapi scan wlan0` lists access points with their
signal levels. Associating needs `wapi psk`, `wapi essid`, and `renew wlan0`.

Two silent failures are guarded against, because both produce an image that builds, flashes, and boots while
the radio never works:

- A missing firmware file makes the NuttX board build substitute a five byte dummy. `make nuttx-build` reads
  the path out of `.config` and checks the file, and stops if it is missing or implausibly small.
- An unpatched tree makes Kconfig drop `CONFIG_RP23XX_INFINEON_CYW43439` without a word, since the symbol is
  defined only by the patch. The driver still compiles, but nothing registers `wlan0`. The build checks for
  the board symbol whenever the driver symbol is set, and says which command to run.

### Wireless Stability Is Unproven

Scanning works, but the board stopped responding some time after `ifup wlan0` and `wapi scan wlan0`. The USB
device stayed enumerated while writes to it timed out, which means the firmware stopped servicing USB rather
than the console session ending. A power cycle recovers it. Which step is responsible is not yet established,
so the sequence to isolate it is to bring the interface up, wait, and check the shell still answers before
scanning.

Enabling wireless disables both LED drivers, because the LED hangs off the wireless chip rather than an RP2350
pin and GPIO 25 is the chip select. Driving it needs the gSPI based user LED driver that
`pimoroni-pico-plus-2-w` carries, which this patch does not port.

The network stack is not free. `bss` grows from about 9 KB to 108 KB, mostly the 196 IOB buffers, leaving
roughly 410 KB of heap instead of 507 KB. Flash use reaches 508864 bytes, which still clears the littlefs
region at `0x100000`.
