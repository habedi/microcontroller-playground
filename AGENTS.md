# AGENTS.md

This file guides coding agents that work on this repository.

## Mission

This repository is a personal playground for learning microcontroller programming.
Apache NuttX is the first OS, but the structure leaves room for other boards and other OSes.
It holds experiments, board configurations, and notes. It is not a product or a library.
Priorities, in order:

1. Working experiments: code that builds, flashes, and runs on the real boards listed below.
2. Reproducible setup: the Nix dev shell, the git submodules under `external/`, and the uv-managed Python tools define the whole environment, so a
   fresh clone can rebuild everything.
3. Understanding: an experiment is done when its notes say what was tried, what worked, and what did not.

## Hardware

The boards connect over USB to a Linux development machine, on either amd64 or arm64.

- ESP32-P4 development board with 32 MB of PSRAM and headers. The ESP32-P4 is a dual-core RISC-V application processor with no radio. The board
  pairs it with an onboard ESP32-C6, which acts as a wireless coprocessor and provides Wi-Fi 6 and Bluetooth LE.
- Raspberry Pi Pico 2 WH: an RP2350 board with headers and a wireless module. The RP2350 has both Arm Cortex-M33 and RISC-V Hazard3 cores. The
  NuttX port targets the Cortex-M33 cores.
- 40-pin male-to-female Dupont jumper wires, 30 cm, for breadboard and peripheral wiring.

## Core Rules

- Use English for code, comments, docs, and notes.
- Prefer small, focused changes over large refactoring.
- Add comments only when they explain something the code does not show.
- Do not add features, error handling, or abstractions beyond what the current experiment needs.
- Never commit changes inside the submodules under `external/` from this repository. A patch that must survive belongs in this repository as a patch
  file or an out-of-tree file. An intentional submodule version bump is its own commit.
- Do not flash a board or open its serial port without being asked. The user may be using the board.

## External Dependencies

External code lives in git submodules under `external/`. It is never copied into the repository:

- `external/nuttx`: the Apache NuttX RTOS kernel and build system.
- `external/nuttx-apps`: the NuttX applications tree. The build expects it next to the kernel.
- `external/pico-sdk`: the Raspberry Pi Pico SDK. The NuttX rp23xx port builds against it through `PICO_SDK_PATH`.
- `external/crosstool-ng`: the Espressif fork of crosstool-NG. It builds the `riscv-none-elf` toolchain that NuttX expects for the Espressif RISC-V
  chips.

After cloning, run `git submodule update --init --depth 1` to fetch them.

Python tools are managed by uv through `pyproject.toml`. That is where `esptool` and `pre-commit` come from. Run them as `uv run esptool` and
`uv run pre-commit`, or from the uv-created virtual environment. Do not install Python tools with pip into the system interpreter.

Everything else (compilers, kconfig tools, flashing and debugging tools) comes from the Nix dev shell in `flake.nix`. Enter it with `nix develop`
before building. A new system-level tool belongs in `flake.nix`. A new Python tool belongs in `pyproject.toml`.

## Repository Layout

- `external/`: the git submodules listed above.
- `experiments/`: one directory per experiment. Each has a `README.md` that names the board, the OS, the build and flash commands, and the result.
- `configs/`: saved build configurations, grouped as `configs/<os>/<board>/<name>/`.
- `docs/`: notes on the boards, wiring, and what each experiment taught.
- `.github/workflows/`: CI workflows.
- `Makefile`: developer tasks; run `make help` to list them. OS-agnostic targets have plain names (`submodules`, `install`, `flash-pico`,
  `console`). OS-specific targets carry the OS as a prefix (`nuttx-configure`, `nuttx-build`, `nuttx-flash-esp`).

Build output stays inside the tree that produced it, under `external/`, and is never committed. Saved configurations, custom applications, and
experiment notes live in this repository, outside `external/`.

## Building and Flashing

All builds run inside the Nix dev shell. The `make nuttx-*` targets wrap the steps below. Configure and run the NuttX build from `external/nuttx`:

1. List the available boards and configurations with `./tools/configure.sh -L`.
2. Configure with `./tools/configure.sh <board>:<config>`. The ESP32-P4 board is `esp32p4-function-ev-board:nsh`, and it is the default board for
   the `make nuttx-*` targets. The Pico 2 is `raspberrypi-pico-2:nsh`, and the ESP32-C6 on its own is `esp32c6-devkitc:nsh`. Run `make distclean`
   before switching boards.
3. Adjust options with `make menuconfig`, then build with `make`.

Flashing depends on the chip family:

- Espressif boards flash over the USB serial port with esptool. Use the NuttX `make flash ESPTOOL_PORT=/dev/ttyUSB0` target, or run `uv run esptool`
  directly. The first flash of a chip also needs the bootloader, which the NuttX build downloads or builds for you.
- The Pico 2 flashes by copying `nuttx.uf2` to the board while it is in BOOTSEL mode, or with `picotool load` over USB.

Reach the serial console with `picocom` (for example `picocom -b 115200 /dev/ttyACM0`). Serial access needs membership in the `dialout` group on most
Linux distributions (some use `uucp` instead).

## Toolchains

- The Pico 2 builds with `arm-none-eabi-gcc`, provided by the dev shell.
- The ESP32-P4 and the ESP32-C6 build with a bare-metal RISC-V toolchain. The dev shell provides `riscv32-none-elf-gcc` from nixpkgs. NuttX defaults
  to the `riscv-none-elf-` prefix, so either set `CROSSDEV=riscv32-none-elf-` on the make command line or build the exact expected toolchain from
  `external/crosstool-ng`.
- Rust experiments use rustup from the dev shell. The targets are `thumbv8m.main-none-eabihf` for the Pico 2, `riscv32imac-unknown-none-elf` for the
  ESP32-C6, and `riscv32imafc-unknown-none-elf` for the ESP32-P4. Add them with `rustup target add`. Flash firmware with `probe-rs` on the Pico 2
  and with `espflash` on the Espressif chips. The dev shell provides both.
- Zig experiments use the `zig` compiler from the dev shell. It cross compiles to all three chips and also serves as a C cross compiler.

## Adding a Board or an OS

The repository is meant to grow past its current boards and past NuttX. The conventions that keep that cheap:

- A new board needs three things: its toolchain and flash tool as a new package list in `flake.nix`, an entry in the Hardware section above, and a
  notes page in `docs/` once it has run something.
- A new OS or SDK gets its source as a submodule under `external/`, its own `<os>-` prefixed group of Makefile targets, its own subdirectory under
  `configs/`, and its build tools as a new package list in `flake.nix`. Nothing about one OS goes into another OS's targets or into the OS-agnostic
  ones.
- An experiment names its board and OS in its `README.md`, not in its directory name, so the same experiment can be redone on other hardware.
- Environment variables an OS needs (such as `PICO_SDK_PATH`) are set in the flake's `shellHook`, not in personal shell configuration, so the setup
  stays reproducible.
- Migration away from an OS is a deletion: remove its submodule, its Makefile target group, its `configs/` subdirectory, and its package list in
  `flake.nix`. If following these conventions would take more than that, the coupling is in the wrong place.

## Writing Style

- Write plain, simple English, in docs and in code comments alike. Use short sentences and everyday words. Keep every fact, name, number, link, and
  file path when you rewrite prose.
- Keep Markdown structure when you rewrite: headings, lists, tables, and links. Do not change fenced code blocks or YAML frontmatter; reproduce them
  exactly.
- Use Oxford commas in inline lists: "a, b, and c" not "a, b, c".
- Do not use em dashes, in documentation or in code comments. Restructure the sentence, or use a colon or semicolon instead.
- Avoid colorful adjectives and adverbs. Write "rate limiter" not "smart rate limiter".
- Prefer noun phrases for checklist items over imperative verbs. Write "rate limit enforcement" not "enforce rate limits".
- Headings in Markdown files must be in title case: "Build from Source" not "Build from source". Minor words stay lowercase unless they are the first
  word: the articles (a, an, the), the coordinating conjunctions (and, but, or, nor, so, yet, for), and the short prepositions (in, on, at, to, by,
  of, up, as, from, with, into, over). The prepositions are named because "from" has to be lowercase for "Build from Source" to be correct.
- Do not bold the lead-in of a list item. Write "Unit tests: ..." not "**Unit tests**: ...".
- Use sentence case for the lead-in of a list item. Write "Seed selection: ..." not "Seed Selection: ...". Proper nouns keep their capitals.
- Capitalize only the first part of a hyphenated compound: "Real-time Scheduling" in a heading, "Real-time" at the start of a sentence, and
  "real-time scheduling" elsewhere. Never write "Real-Time".
- Start each sentence with a capital letter, capitalize proper nouns (NuttX, RISC-V, Arm, Espressif, Wi-Fi), and leave common nouns lowercase in the
  middle of a sentence.
- Write correct and complete sentences.
- Avoid made-up words.
- Do not use a colon in place of a verb. Three uses are fine: joining two clauses inside a complete sentence (the replacement the em-dash rule above
  calls for), introducing the gloss of a list item, and introducing an enumeration, whether as a list or inline ("Boards: the ESP32-P4, the
  ESP32-C6, ..."). What a colon must not do is turn a sentence into a label and a definition: write "Copies the firmware to the board over USB"
  rather than "Flashing: copies the firmware to the board". That shape belongs to a list item. Carrying it into prose (a doc comment summary, a
  paragraph) leaves a fragment where a sentence was required.
- Use participial phrases and abbreviations scarcely.

## Experiment Flow

1. Read the notes in `docs/` for the board involved, if any exist.
2. Configure and build the smallest NuttX image that tests the idea. Start from the `nsh` configuration in most cases.
3. Flash it, check the behavior on the serial console or on the wired hardware, and record the result in `docs/`.
4. Save any board configuration worth keeping into this repository, so it can be rebuilt after a `make distclean`.

## Testing

There is no repository-wide test suite, and red-green TDD is not required. Hardware behavior (pins, timing, radios, power) cannot fail in CI, so it
is verified on the real board, as the Validation section below describes.

Logic that does not need the hardware is the exception. Parsers, protocol framing, and pure algorithms get host-side unit tests inside their
experiment directory, and those follow the red-green cycle: write the test, watch it fail for the expected reason, then make it pass. Host tests are
the part of an experiment that survives a move to another board or OS, so prefer pushing logic into host-testable form over testing on the board.

## Validation

The boards are the test bench for everything else. Before committing a change:

1. The affected configuration builds from a clean tree inside the dev shell.
2. The image was flashed and seen running on the real board, and the notes say so.
3. Nothing under `external/` was modified, and no build output is staged.

## Commit Hygiene

- Keep commits scoped to one logical change, and keep a submodule version bump as its own commit.
- Say in the commit message which board the change was tested on, since behavior differs across chips.
