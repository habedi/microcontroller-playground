# AGENTS.md

This file guides coding agents working on this repository.

## Mission

This repository is a personal playground for learning microcontroller programming across Apache NuttX and other RTOSes. It holds experiments,
configurations, and notes.
Priorities, in order:

1. Working experiments: code that builds, flashes, and runs on hardware.
2. Reproducible setup: the Nix dev shell, submodules under `external/`, and uv define the entire environment.
3. Understanding: an experiment is done when notes document what was tried, what worked, and what failed.

## Hardware

The boards connect over USB to a Linux development machine (amd64 or arm64):

- Waveshare ESP32-P4-WIFI6: ESP32-P4NRW32 (dual-core RISC-V, 32 MB PSRAM, 32 MB flash) with an onboard ESP32-C6 over SDIO (functional after
  flashing native SDIO slave firmware; see `docs/esp32p4.md`). Silicon revision v1.3 requires `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` and `CONFIG_ESP32P4_REV_MIN_100`. USB-C links to a WCH CH343
  UART0 bridge (`/dev/ttyACM0`, vendor `1a86`). NuttX board target is `esp32p4-function-ev-board`.
- Raspberry Pi Pico 2 WH: RP2350 (Arm Cortex-M33 and RISC-V Hazard3 cores) with CYW43439 wireless. NuttX targets Cortex-M33 using board
  `raspberrypi-pico-2`. The `usbnsh` configuration provides console over USB. See `docs/raspberrypi-pico-2.md`.
- Waveshare ESP32-S3-Zero: ESP32-S3FH8R8 (dual-core Xtensa LX7, 8 MB flash, 8 MB Octal PSRAM) with an onboard WS2812 RGB LED on GPIO 21.
  USB-C links directly to the internal USB Serial/JTAG controller (`/dev/ttyACM0`, vendor `303a`). See `docs/esp32s3.md`.
- 40-pin male-to-female Dupont jumper wires, 30 cm, for peripheral wiring.

## Core Rules

- Use English for code, comments, docs, and notes.
- Prefer small, focused changes over large refactoring.
- Add comments only when they explain something the code does not show.
- Do not add features, error handling, or abstractions beyond current experiment needs.
- Never commit changes inside submodules under `external/`. Out-of-tree patches belong in `patches/`. Submodule version bumps belong in their own
  commit.
- Check the nested Espressif HAL repository at `external/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty` directly to verify it is clean.
- Do not flash a board or open its serial port without being asked.
- Preserve the onboard ESP32-C6 SDIO slave firmware. It provides the P4's wireless access. Do not erase or flash the C6, including for Rust
  experiments, unless the user explicitly authorizes replacing its wireless firmware and accepts the loss of wireless access. A general request
  to build, test, or flash the P4 does not authorize changing the C6. Keep C6 experiments build-only by default.

## External Dependencies

External code lives in git submodules under `external/` (fetch with `make submodules`):

- `external/nuttx`: Apache NuttX RTOS kernel and build system.
- `external/nuttx-apps`: NuttX applications tree.
- `external/pico-sdk`: Raspberry Pi Pico SDK (`PICO_SDK_PATH`).
- `external/esp-idf`: Espressif IoT Development Framework (v6.1; `make espidf-install` places toolchains in `build/espressif`).
- `external/crosstool-ng`: Espressif crosstool-NG fallback toolchain.

Submodules use `ignore = dirty` in `.gitmodules`. In JetBrains IDEs, map only the repository root under Settings, Version Control, Directory Mappings
to avoid accidental submodule commits.

ESP-IDF managed components land in `managed_components/` (gitignored; pinned by `dependencies.lock`). Document component rationale in
`idf_component.yml`.

Python tools are managed by uv via `pyproject.toml` (run with `uv run`). System compilers and tools come from `flake.nix` (`nix develop`).

## Repository Layout

- `external/`: git submodules.
- `experiments/`: one directory per experiment with a `README.md`.
- `configs/`: saved build configurations under `configs/<os>/<board>/<name>/`.
- `docs/`: hardware, wiring, and architectural documentation.
- `Makefile`: developer tasks (`make help`).
- Build artifacts remain inside the producing tree under `external/` or `build/` and are never committed.

## Building and Flashing

Builds run inside the Nix dev shell (`make shell`):

1. Configure via `make nuttx-configure BOARD=<board>:<config>`. Defaults: `esp32p4-function-ev-board:nsh` (ESP32-P4) and `raspberrypi-pico-2:nsh`
   (Pico 2).
2. Clean rebuilds (`make nuttx-distclean`) are required after configuration or flag changes, as NuttX does not reliably rebuild vendored HAL objects.
3. Build with `make nuttx-build`.

Flashing:

- Espressif boards: flash over USB serial with `make nuttx-flash-esp PORT=<port>` or `uv run esptool`.
- Pico 2: flash in BOOTSEL mode via `make flash-pico-uf2` (copies UF2 to mass storage) or `make flash-pico` (picotool; requires `make setup-udev`).

Serial console:

- Open with `picocom -b 115200 /dev/ttyACM0` (requires `dialout` group).
- Prefer the `tools/board-mcp.py` MCP server, which serializes access, handles contention, and scrubs hardware identifiers. See `docs/board-mcp.md`.

## Toolchains

- Pico 2: builds with `arm-none-eabi-gcc` from the dev shell.
- ESP32-P4 and ESP32-C6: build with custom `riscv32-none-elf-gcc` (GCC 14, `rv32imac`, `ilp32`) provided by `flake.nix`.
- ESP32-S3: builds with the Xtensa Rust compiler (`esp` channel from `espup`) targeting `xtensa-esp32s3-espidf` with `ldproxy`.
- Prefix: `make nuttx-*` passes `CROSSDEV=riscv32-none-elf-` automatically for RISC-V builds.
- Rust: provided by `rust-overlay` with targets `thumbv8m.main-none-eabihf`, `riscv32imac-unknown-none-elf`, and `riscv32imafc-unknown-none-elf`
  pre-installed. Flash with `probe-rs` (Pico 2) or `espflash` (ESP32).
- Zig: pinned to 0.17.0 via `zig-overlay`. Cross compiles to all three chips and serves as a C cross compiler.
- Bare metal Rust: `make rust-build` builds `experiments/serial-heartbeat` with esp-rs for the ESP32-C6. The root `Cargo.toml` defines the workspace
  and release profile; the root `Cargo.lock` pins dependencies. Target settings belong in the root `.cargo/config.toml`.
  Current esp-hal P4 support uses v3.x clock and ROM interfaces; do not bypass the minimum revision check for this board's v1.3 silicon.
- MicroZig: `make zig-build` uses the root `build.zig` to build `experiments/gpio-blink` for the Pico 2 WH.
  MicroZig 0.17.20 is pinned in the root `build.zig.zon` for Zig 0.17.0.
  Use an external LED on GP0; GP25 connects to the wireless chip on this board.

## Adding a Board or an OS

- New board: add package list in `flake.nix`, entry in Hardware above, and notes in `docs/`.
- New OS or SDK: add submodule under `external/`, prefixed Makefile targets (`<os>-*`), subdirectory in `configs/`, and packages in `flake.nix`.
- OS environment variables belong in `flake.nix` `shellHook`.
- Experiments name boards and OS in `README.md`, not directory names.
- Migration away from an OS removes its submodule, target group, configs, and packages without a residue.

## Writing Style

- Write plain, simple English with short sentences and everyday words.
- Maintain Markdown structure: title case for `##` and `###` headings, sentence case for `####` and deeper.
- Use Oxford commas in inline lists: "a, b, and c".
- Do not use em dashes; use colons or semicolons instead.
- Avoid colorful adjectives and adverbs.
- Do not bold the lead-in of list items; use a sentence case.
- Capitalize proper nouns (NuttX, RISC-V, Arm, Espressif, Wi-Fi).
- Do not use a colon in place of a verb in prose.
- Keep fenced code blocks and YAML frontmatter exactly as defined.

## Experiment Flow and Testing

1. Check existing board notes in `docs/`.
2. Configure and build the smallest image testing the concept (usually starting from `nsh`).
3. Flash, verify behavior on hardware, and document findings in `docs/`.
4. Save reproducible configurations under `configs/`.

Hardware behavior is verified on physical boards. Logic independent of hardware (parsers, protocol framing) must have host-side unit tests under
`experiments/<name>/test/`.

## Validation

Before committing:

1. Affected configurations build cleanly after `make nuttx-distclean`.
2. Image flashed and verified on physical hardware, documented in notes.
3. Submodules under `external/` (including nested HALs) are clean, and no build output is staged.

## Privacy

This repository is public. Never commit:

- Absolute paths: use `PLAYGROUND_ROOT` (check saved defconfigs).
- Hardware identifiers: no MAC addresses, eFuse contents, or serial numbers.
- Wireless survey data: record scan success and count, never SSIDs or BSSIDs.
- Personal notes or pricing: store in `*.local.md` (gitignored).
- Unfiltered console output: quote only relevant technical lines.

## Commit Hygiene

- Scope commits to one logical change; submodule updates belong in separate commits.
- State in the commit message which board the change was tested on.
