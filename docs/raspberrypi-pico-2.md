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
- `configs/nuttx/raspberrypi-pico-2/usbnsh-lcd13/defconfig` is `usbnsh-tools` plus the ST7789 driver, the
  framebuffer, and `lcddev`, for the 1.3 inch 240x240 display. It needs the patch described under Two Things
  the Tree Was Missing below.
- `configs/nuttx/raspberrypi-pico-2/usbnsh-lcd-wifi/defconfig` combines the two, and adds `CONFIG_FS_TMPFS`
  and `CONFIG_ETC_ROMFS` so `/data` and `/tmp` are mounted before the shell starts. It gives the littlefs
  region the rest of the flash, 3 MB rather than 1 MB. `CONFIG_ARCH_LEDS` has to stay off, because the
  CYW43439 patch compiles out the LED sources that `board_autoled_on` lives in.
- `configs/nuttx/raspberrypi-pico-2/usbnsh-wamr/defconfig` is `usbnsh-littlefs` plus WAMR (`iwasm`), Zmodem
  file transfer (`rz`), `CONFIG_ELF`, and `CONFIG_ETC_ROMFS` so the 3 MB `/data` volume is mounted at boot.

Rebuild any of them with:

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
flash and give it 1 MB, which clears the image with room to spare. The `usbnsh-lcd-wifi` configuration keeps
the offset and takes the rest of the chip instead, so the region is 3 MB:

```
CONFIG_RP23XX_FLASH_MTD_OFFSET=0x100000
CONFIG_RP23XX_FLASH_MTD_SIZE=0x300000
```

The driver refuses to initialize if the region would overlap `__flash_binary_end`, so the offset has to stay
beyond the end of the image, on a 4096 byte erase sector boundary.

The bringup code mounts nothing, apart from XIPFS when `CONFIG_FS_XIPFS` is set. Format and mount on the
first boot, then mount without the option afterwards:

```
mount -t littlefs -o autoformat /dev/rpflash /mnt
mount -t littlefs /dev/rpflash /mnt
```

`-o` has to come before the paths. `cmd_mount` parses with getopt, which stops at the first argument that is
not an option, so a trailing `-o autoformat` is read as two more paths and the command fails with "too many
arguments". The `usbnsh-lcd-wifi` configuration mounts it at boot instead; see the section below.

Changing `CONFIG_RP23XX_FLASH_MTD_SIZE` changes the geometry littlefs expects, so a region formatted at the
old size no longer mounts. The failure is `EINVAL`, and `autoformat` does not cover it, because that option
only fires on `EFAULT`. Reformat once with `-o forceformat` after a size change.

`df` then reports 4096 byte blocks, 256 of them at 1 MB and 768 at 3 MB. Files written to `/mnt` survive `reboot`, a power cycle, and
reflashing the firmware, because the region starts beyond the image. An image that grew past the 1 MB offset
would collide with it, and the driver refuses to initialize in that case rather than corrupting the data.

### Mounting at Boot

`CONFIG_ETC_ROMFS` and `CONFIG_FS_ROMFS` build the board's `src/etc` directory into a small ROMFS image,
mount it at `/etc`, and run `etc/init.d/rcS` before the shell starts. The board ships both `rcS` and
`rc.sysinit` with nothing in them but a license header, so the mounts go in `rcS`, which
`patches/nuttx/0003-rp23xx-rcS-mount-storage.patch` does:

```
mount -t tmpfs /tmp
mount -t littlefs -o autoformat /dev/rpflash /data
```

The file passes through the C preprocessor, so `#ifdef` guards work and a `#` cannot start a comment. Use
`/* */` instead. The generated copy lands in `boards/arm/rp23xx/common/etctmp/etc/init.d/rcS`, which is worth
reading when a line does not do what it should.

After this, `mount` reports four filesystems on a fresh boot:

| Mount   | Type     | Size | Backing                       |
| ------- | -------- | ---- | ----------------------------- |
| `/data` | littlefs | 3 MB | internal flash, `/dev/rpflash` |
| `/tmp`  | tmpfs    | RAM  | grows as it is used           |
| `/etc`  | romfs    | 384 B | read-only, holds `rcS`       |
| `/proc` | procfs   | 0 B  | kernel state                  |

A file written to `/data` survives a reset, a power cycle, and a reflash.

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


### The 1.3 Inch 240x240 Display

A Waveshare Pico-LCD-1.3, an ST7789 panel with a joystick and four buttons, works over SPI1.
It seats onto all 40 pins, so nothing needs wiring.

```shell
make nuttx-distclean
make nuttx-configure-saved SAVED_CONFIG=configs/nuttx/raspberrypi-pico-2/usbnsh-lcd13
make nuttx-build
make flash-pico-uf2 UF2=external/nuttx/nuttx.uf2
```

The image is 348672 bytes. It registers `/dev/fb0` and `/dev/lcd0`, and the `fb` example draws nested coloured
rectangles on the panel:

```
nsh> fb
     xres: 240
     yres: 240
      bpp: 16
    fblen: 115200
   stride: 480
Test finished
```

115200 bytes is 240 by 240 at two bytes per pixel, so the geometry is right.

### Two Things the Tree Was Missing

NuttX has the chip-independent `st7789.c` driver, but the rp23xx board support could not reach it. Both gaps are
fixed by `patches/nuttx/0002-rp23xx-st7789-lcd.patch`.

`common/src/rp23xx_st7789.c` did not exist, although both `Make.defs` and `CMakeLists.txt` already listed it
under `CONFIG_LCD_ST7789`. The new file follows `rp23xx_st7735.c`, which is the same pattern already native to
this chip. Only the backlight pin differs: GP13 here against GP25 on the panel that file was written for.

`common/src/rp23xx_common_bringup.c` registered no display at all. It had no `fb_register()`, no
`board_lcd_initialize()`, and no `lcddev_register()`, so nothing would ever have called the glue. The patch adds
those three, guarded by `CONFIG_VIDEO_FB`, `CONFIG_LCD`, and `CONFIG_LCD_DEV`, mirroring
`rp2040_common_bringup.c`. This is also why `rp23xx_st7735.c` sits in the tree with no board configuration using
it.

### Pins

The Waveshare wiring matches the rp23xx SPI1 defaults, so no pin options need setting:

| Signal | GPIO | Source |
| --- | --- | --- |
| CLK | 10 | `CONFIG_RP23XX_SPI1_SCK_GPIO` |
| DIN | 11 | `CONFIG_RP23XX_SPI1_TX_GPIO` |
| CS | 9 | `CONFIG_RP23XX_SPI1_CS_GPIO` |
| DC | 8 | `CONFIG_RP23XX_SPI1_RX_GPIO` |
| RST | 12 | `LCD_RST` in the board glue |
| BL | 13 | `LCD_BL` in the board glue |

D/C reuses the SPI1 RX pad. The panel bus is write-only, so RX is free, and `rp23xx_spi1cmddata()` in
`common/src/rp23xx_spi.c` already drives `CONFIG_RP23XX_SPI1_RX_GPIO` for exactly this. That option has to stay
at its default of 8.

### Controls

The panel carries a five way joystick and four keys. They are plain GPIO pads that short to ground, so each pad
needs its pull-up on and a pressed control reads low. The board configured the pads with no pull-up at all, which
left an unpressed input floating and reading as noise, so the patch turns them on.

`patches/nuttx/0006-raspberrypi-pico-2-lcd13-buttons.patch` declares the nine controls, fixes the pull-ups, and
has the board select `ARCH_HAVE_BUTTONS` and `ARCH_HAVE_IRQBUTTONS`. Without those two selects `CONFIG_ARCH_BUTTONS`
cannot be set at all, since it depends on them. The patch also adds the `nuttx/input/buttons.h` include that
`rp23xx_bringup.c` was missing: it called `btn_lower_initialize()` without it, which failed the build the first
time `CONFIG_INPUT_BUTTONS` was enabled on this board.

With `CONFIG_INPUT_BUTTONS` the bringup registers `/dev/buttons`, and a read returns one bit per control.

| Control | GPIO | Bit |
| --- | --- | --- |
| Joystick up | 2 | 0 |
| Joystick down | 18 | 1 |
| Joystick left | 16 | 2 |
| Joystick right | 20 | 3 |
| Joystick press | 3 | 4 |
| Key A | 15 | 5 |
| Key B | 17 | 6 |
| Key X | 19 | 7 |
| Key Y | 21 | 8 |

None of these clash with the panel, which uses GPIO 8 to 13, or with the wireless chip, which uses 23 to 25 and
29. The bit order follows the `BUTTON_` definitions in the board's `board.h` rather than the GPIO numbers, so a
reader that cares about the order has to follow that file.

The mapping was settled by using the controls and watching what they did, not by a clean scan.
`apps/examples/buttons` reported eight distinct bits for nine presses, and it is a poor tool for the job: it
stops printing after a handful of samples while its daemon stays alive and goes on consuming the events, so a
second capture returns nothing. Which of the nine went unreported was never established. If a single control
misbehaves later, that missing press is the first thing to re-check.

### Stopping a Task

`kill` does not stop a task in these configurations. `CONFIG_SIG_DEFAULT` is off, so there is no default action
for `SIGKILL` and the target ignores it, `kill -9` included. A long running application needs its own way out.
The face reads the word `quit` from its state file for exactly this reason.

### WebAssembly (WAMR)

The `usbnsh-wamr` configuration enables the WebAssembly Micro Runtime (WAMR) through `CONFIG_INTERPRETERS_WAMR`,
`CONFIG_INTERPRETERS_IWASM_TASK`, `CONFIG_INTERPRETERS_WAMR_CLASSIC`, and `CONFIG_INTERPRETERS_WAMR_LIBC_BUILTIN`.

The image builds at 295860 bytes of flash (about 7.1 percent of the 4 MB chip) and 18900 bytes of static RAM
(about 3.6 percent of the 512 KB main SRAM), leaving more than 490 KB of heap.

Running a WebAssembly binary from the `/data` LittleFS storage:

```
nsh> iwasm /data/hello.wasm
```

Pass runtime parameters when needed:

```
nsh> iwasm --stack-size=16384 --heap-size=32768 /data/app.wasm
```

#### Transferring files to `/data`

The `usbnsh-wamr` configuration includes Zmodem via `CONFIG_SYSTEM_ZMODEM` with `CONFIG_SYSTEM_ZMODEM_MOUNTPOINT`
set to `/data`. This allows transferring `.wasm` files over the USB serial console without reflashing:

1. In the NuttShell console, start the receiver:
   ```
   nsh> rz
   ```
2. On the host development machine, send the file using `sz` from `picocom` (via `Ctrl-A` then `Ctrl-S`), or
   from the command line:
   ```shell
   sz -b -vv app.wasm > /dev/ttyACM0 < /dev/ttyACM0
   ```
3. The received file lands in `/data/app.wasm`, preserved across software resets and reflashing.

#### RAM requirements on RP2350

The RP2350 carries 520 KB of SRAM (512 KB in the main banks plus two 4 KB scratch banks) and no external PSRAM.
With the base system consuming less than 20 KB of RAM, more than 490 KB of heap is available.

Running a WebAssembly module consumes RAM across four areas:

- WAMR task stack: `CONFIG_INTERPRETERS_WAMR_STACKSIZE` defaults to 8192 bytes.
- Binary file buffer: `bh_read_file_to_buffer()` loads the entire `.wasm` binary into heap during execution.
  A 20 KB binary consumes 20 KB of heap.
- WebAssembly operand stack: `iwasm` defaults to 64 KB (`--stack-size`), but modules that avoid deep recursion
  run comfortably with 8 KB or 16 KB.
- Linear memory: WebAssembly allocates memory in 64 KB pages. A module declaring `(memory 1)` allocates 64 KB,
  while `(memory 4)` allocates 256 KB. WebAssembly compilers like `wasm-ld` default to 16 pages (1 MB) for the
  linear memory C stack, which exceeds physical SRAM; compile with `-Wl,-z,stack-size=4096 -Wl,--initial-memory=65536 -Wl,--max-memory=131072`
  to constrain it to 1 or 2 pages.

A typical embedded module using one memory page and a 16 KB execution stack uses approximately 100 KB to 120 KB
of RAM in total, leaving more than 350 KB of free heap on the RP2350. Modules requiring more than 6 pages (384 KB)
will exhaust available heap.

### Loadable Native Applications (ELF)

The RP2350 configuration supports `CONFIG_BINFMT_LOADABLE` and `CONFIG_ELF`.

#### Board and architecture support

The RP2350 uses an Arm Cortex-M33 core (`CONFIG_ARCH_ARMV8M=y`). NuttX provides full ARMv8-M Thumb-2 relocation
support in `libs/libc/machine/arm/armv8-m/arch_elf.c` (`up_checkarch()`, `up_relocate()`, and `up_relocateadd()`).
SRAM on the RP2350 (0x20000000 to 0x20082000) is executable. The binary loader reads an ELF file from `/data`,
allocates SRAM from the system heap for text and data sections, resolves relocations, invalidates the instruction
cache, and branches to the module entry point.

#### Application build environment

Loadable applications are built as relocatable ELFs (partial linking with `-r`):

1. Compiler flags:
   ```
   arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
     -mlong-calls -fno-common -c app.c -o app.o
   ```
2. Linker flags:
   ```
   arm-none-eabi-ld -r -e main -T <nuttx-export>/scripts/gnu-elf.ld app.o -o app.elf
   ```
3. Export package: `make export` packages the NuttX libraries, header files, and `gnu-elf.ld` linker script into
   `nuttx-export-*.zip`. Applications built outside the tree compile and link against this export directory.
4. Symbol resolution: In flat builds, undefined references to system functions (`printf`, `malloc`, and POSIX
   APIs) require an exported symbol table. NuttX provides symbol tables via `tools/mksymtab.c` registered with
   `boardctl(BOARDIOC_APP_SYMTAB)` or `CONFIG_EXECFUNCS_SYMTAB_ARRAY`.

### Not Done Yet

The panel is 240 pixels wide while NES output is 256 and SNES is 256, so an emulator would have to crop or scale
horizontally.

### Cold Boot Is Intermittent

A cold boot sometimes leaves the board unreachable. The USB device enumerates and the host can write to it,
so the firmware runs and services USB, but nothing answers on the console: no prompt, no echo, nothing across
fifteen seconds of listening and eight Enters. Reflashing recovers it, because that ends in a warm reset.

What is known:

- The failure appeared once in three cold boots of an image with the network stack enabled, and once on the
  wireless image.
- Ten consecutive software reboots, driven by the `reboot` command, all came back alive, so `reboot` does not
  reproduce it.
- Delaying the host by thirty seconds before opening the port does not cause it. That was tested directly.
- The plain `usbnsh-littlefs` image has not been seen to fail, but it has had far fewer cold boots, so this is
  not evidence that it is immune.

The cause is unknown. Wireless was suspected first and cleared, since an image with the network stack and no
wireless driver failed the same way. Host timing was suspected next and cleared by the delayed-open test.

Two consequences for testing. A single passing cold boot proves nothing about an intermittent fault, so a
claim about cold boot needs repeated trials. Flashing is a warm reset, so testing only after a flash hides
this class of failure entirely.
