## Serial Heartbeat

This experiment runs bare metal Rust on the Waveshare board's onboard ESP32-C6 with
[esp-rs esp-hal](https://github.com/esp-rs/esp-hal). It prints `Rust heartbeat` once
per second on the C6 UART0 at 115200 baud. It uses no RTOS, allocator, or Wi-Fi stack.

### Build

From the repository root:

```shell
make shell
make rust-build
# Or use Cargo directly from the root:
cargo build --release --locked -p serial-heartbeat
```

The ELF is `build/rust/riscv32imac-unknown-none-elf/release/serial-heartbeat`.
The root `Cargo.toml` defines the workspace and release profile. Dependencies are
pinned by the root `Cargo.lock`, and the root `.cargo/config.toml` selects the C6
target, linker script, and output directory. Rust and the target libraries come from the
locked Nix dev shell. The C6 uses upstream Rust; it does not need the Xtensa compiler
fork or `espup`.

### Flash and Verify

Keep this experiment build-only on the onboard C6 by default. Do not erase or flash
it without explicit user authorization to replace its SDIO slave firmware and lose
P4 wireless access. A general request to test Rust or flash the P4 does not authorize
changing the C6. Prefer a separate ESP32-C6 board for hardware tests.

The board's USB-C bridge connects to the P4, not the C6. Accessing the C6 needs the
UART adapter, soldered pads, and boot mode procedure in [the board notes](../../docs/esp32p4.md).
Hold the P4 in its bootloader so it does not drive the C6 bus. Do not connect the
adapter's VDD pin. Only run this when replacing the C6 firmware is intended:

```shell
espflash flash --chip esp32c6 --port /dev/ttyUSB0 \
  build/rust/riscv32imac-unknown-none-elf/release/serial-heartbeat
make console TTY=/dev/ttyUSB0
```

Expect `Rust heartbeat` once per second after reset. This replaces the SDIO slave
firmware and stops P4 wireless access. Restore it with the procedure in the board
notes after the experiment.

### Status

The firmware builds in the Nix dev shell. It has not been flashed or verified on
hardware. The loop only calls HAL and console functions; there is no hardware-free
logic to unit test.

The ESP32-P4 v1.3 on this board is not a target for this experiment. Current esp-hal
P4 support uses revision v3.x clock and ROM interfaces. Lowering its minimum revision setting does not provide support for earlier silicon.
