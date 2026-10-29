## GPIO Blink

This experiment runs bare metal Zig 0.17.0 on the Raspberry Pi Pico 2 WH (RP2350,
Arm Cortex-M33) with [MicroZig 0.17.20](https://github.com/ZigEmbeddedGroup/microzig/releases/tag/0.17.20).
MicroZig supplies startup code, the linker script, the HAL, and UF2 generation.
The dependency URL and content hash are pinned in the root `build.zig.zon`.

The build follows the MicroBuild pattern from the Zig MCU template, updated for
Zig 0.17.0. It uses the `pico2_arm` target for the RP2350; it does not initialize
the wireless chip.

### Wiring

Connect GP0 (physical pin 1) through a 330 ohm resistor to an external LED anode.
Connect its cathode to GND (physical pin 3). See [the board notes](../../docs/raspberrypi-pico-2.md).
The LED changes state every 500 ms. A logic analyzer on GP0 can check the same signal.

The onboard LED on the Pico 2 WH belongs to the CYW43439. GP25 is part of its wireless
interface, so the plain Pico 2 template's GP25 blinky must not be used on this board.

### Build

From the repository root:

```shell
make shell
make zig-build
# Or use Zig directly from the root:
zig build -Doptimize=ReleaseSmall --prefix build/zig
```

The first build downloads MicroZig and its dependencies through Zig's package manager.
The root `build.zig` builds the firmware from this experiment's source directory.
The output directory is `build/zig/firmware/`, with ELF and UF2 images.
Without `--prefix`, Zig uses its standard `zig-out/` directory.
Keep Zig and MicroZig versions compatible when upgrading either pin.

### Flash and Verify

Hold BOOTSEL while connecting the board, then run:

```shell
make flash-pico-uf2 UF2=build/zig/firmware/gpio-blink.uf2
```

Expect one complete blink per second on the external LED. This replaces the current
firmware. It provides no USB serial console.
