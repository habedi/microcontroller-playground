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
  pairs it with an onboard ESP32-C6, which acts as a wireless coprocessor and provides Wi-Fi 6 and Bluetooth LE. This sample is silicon revision
  v1.3, below the v3.0 that NuttX supports, so it needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` and
  `CONFIG_ESP32P4_REV_MIN_100` to get past the revision check. With those set it reaches a NuttShell prompt when the console is on UART0, and it also
  runs ESP-IDF. Only the USB Serial/JTAG console is broken, so use the UART one. It has two USB-C connectors: one goes to the chip's
  own USB Serial/JTAG controller, and the other goes to an onboard WCH CH343 bridge on UART0, meaning GPIO37 and GPIO38. Both enumerate as
  `/dev/ttyACM0`, so identify them by USB vendor, `303a` for Espressif and `1a86` for the bridge. The bridge gives a UART0 console with no extra
  hardware.
- Raspberry Pi Pico 2 WH: an RP2350 board with headers and a wireless module. The RP2350 has both Arm Cortex-M33 and RISC-V Hazard3 cores. The
  NuttX port targets the Cortex-M33 cores. The NuttX board is `raspberrypi-pico-2`, since the tree has no `-w` variant, and it runs correctly on the
  WH with the wireless module unused. The `usbnsh` configuration gives a NuttShell over the USB cable, which makes this the board that currently
  works without extra hardware. Wiring up the CYW43439 would mean borrowing from `boards/arm/rp23xx/pimoroni-pico-plus-2-w`, since the driver at
  `arch/arm/src/rp23xx/rp23xx_cyw43439.c` is not wired up for this board. See `docs/raspberrypi-pico-2.md`.
- 40-pin male-to-female Dupont jumper wires, 30 cm, for breadboard and peripheral wiring.

## Core Rules

- Use English for code, comments, docs, and notes.
- Prefer small, focused changes over large refactoring.
- Add comments only when they explain something the code does not show.
- Do not add features, error handling, or abstractions beyond what the current experiment needs.
- Never commit changes inside the submodules under `external/` from this repository. A patch that must survive belongs in this repository as a patch
  file or an out-of-tree file. An intentional submodule version bump is its own commit.
- Watch for repositories nested inside a submodule. The Espressif HAL at `external/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty` is its own git
  repository, cloned by the NuttX build and ignored by NuttX itself, so `git -C external/nuttx status` reports a clean tree while edits sit there
  unnoticed. Check it directly before concluding that the submodules are untouched.
- Do not flash a board or open its serial port without being asked. The user may be using the board.

## External Dependencies

External code lives in git submodules under `external/`. It is never copied into the repository:

- `external/nuttx`: the Apache NuttX RTOS kernel and build system.
- `external/nuttx-apps`: the NuttX applications tree. The build expects it next to the kernel.
- `external/pico-sdk`: the Raspberry Pi Pico SDK. The NuttX rp23xx port builds against it through `PICO_SDK_PATH`.
- `external/esp-idf`: the Espressif IoT Development Framework, pinned at v5.5.5. It is the vendor SDK for the ESP32 chips and builds on FreeRTOS. It
  carries its own submodules, so it needs a recursive fetch, and its compilers are prebuilt downloads rather than Nix packages. `make espidf-install`
  places them under `build/espressif` instead of the home directory.
- `external/crosstool-ng`: the Espressif fork of crosstool-NG. It can build a bare-metal RISC-V toolchain for the Espressif chips. The dev shell
  builds its own compiler through the flake instead, so this submodule is a fallback rather than part of the normal setup.

JetBrains IDEs need one manual setting. They detect every nested `.git` directory and register it as its own
version control root, then group the commit dialog by root, so a node named after a submodule appears there
holding that submodule's changed files. Committing with it checked creates a commit inside the submodule, which
the Core Rules forbid. The IDE does not consult `ignore = dirty` when building that list. Remove the
`external/...` rows under Settings, Version Control, Directory Mappings, leaving only the project root, and
decline the offer to re-add them when the IDE reports unregistered roots.

Each submodule carries `ignore = dirty` in `.gitmodules`. A configured and built tree is dirty by design, since
NuttX builds in place and `make nuttx-patch` edits the submodule, so that setting keeps the noise out of
`git status` while still showing a moved pointer, which is a real change and belongs in its own commit. To
inspect the dirt deliberately, use `git status --ignore-submodules=none` or run git inside the submodule.

After cloning, run `make submodules` to fetch them. A plain `git submodule update --init --depth 1` misses the nested submodules that the Pico SDK
and ESP-IDF need.

ESP-IDF pulls some code through its own component manager rather than as a submodule. Those land in
`managed_components/` inside the project that asked for them, a path ESP-IDF does not let you move. That
directory is gitignored, and `dependencies.lock` next to it is committed instead, since the lock file is the
pin and the fetched source is a cache. This follows the same split as `flake.lock` and `uv.lock` against
`.venv/`. A component belongs in `idf_component.yml`, alongside a comment saying why, not in `external/`.

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

Any change to the configuration needs a clean rebuild. NuttX does not reliably recompile the vendored Espressif HAL objects when `.config` changes,
so an incremental build can link objects compiled under the previous options. That has already produced a phantom linker error, whose workaround
resolved a function to address zero and shipped it. After `kconfig-tweak`, `menuconfig`, or any other configuration edit, run `make nuttx-distclean`,
configure again, and build from scratch. When a rebuild produces a byte-identical image after a configuration change, the build was skipped, not
unnecessary.

Flashing depends on the chip family:

- Espressif boards flash over the USB serial port with esptool. Use the NuttX `make flash ESPTOOL_PORT=<port>` target, or run `uv run esptool`
  directly. The port depends on the board. Either ESP32-P4 connector works for flashing, since the ROM download mode listens on UART0 as well as on
  USB Serial/JTAG, and both appear as `/dev/ttyACM0`. Boards that use a CP2102 bridge appear as `/dev/ttyUSB0` instead. The first flash of a chip also needs the bootloader,
  which the NuttX build downloads or builds for you.
- The Pico 2 flashes in BOOTSEL mode, either with `make flash-pico-uf2`, which copies `nuttx.uf2` to the drive the board exposes and needs no special
  permissions, or with `make flash-pico`, which uses `picotool` and needs the udev rules that `make setup-udev` installs once. Getting into BOOTSEL
  mode always takes a physical button press, because NuttX exposes no picotool reset interface and `picotool reboot -f -u` therefore cannot do it.
  Every test on that board costs the user a button press, so ask for one deliberately and change one thing at a time.

Reach the serial console with `picocom` (for example `picocom -b 115200 /dev/ttyACM0`). Serial access needs membership in the `dialout` group on most
Linux distributions (some use `uucp` instead).

## Toolchains

- The Pico 2 builds with `arm-none-eabi-gcc`, provided by the dev shell.
- The ESP32-P4 and the ESP32-C6 build with a bare-metal RISC-V toolchain. The dev shell builds its own `riscv32-none-elf-gcc`, because the stock
  nixpkgs one does not work here. That compiler carries no multilib and only an `ilp32d` libgcc, which cannot link the soft-float `rv32imac` code
  NuttX generates for either chip. The flake pins a `crossSystem` with `gcc.arch = "rv32imac"` and `gcc.abi = "ilp32"` instead, and takes GCC 14
  rather than 15, since GCC 15 defaults to C23 and C23 removed the `ATOMIC_VAR_INIT` macro that the vendored Espressif HAL still uses. The build is
  from source and takes about an hour on a cold Nix store, so run `make shell-pin` afterwards to keep a garbage collection from discarding it.
- These board configurations select `CONFIG_RISCV_TOOLCHAIN_GNU_RV64`, so NuttX looks for `riscv64-unknown-elf-`, not the `riscv-none-elf-` prefix.
  The `make nuttx-*` targets read the configured architecture and pass `CROSSDEV=riscv32-none-elf-` when a RISC-V build finds that compiler on PATH,
  so the flag does not belong on the command line. A machine using the Debian `gcc-riscv64-unknown-elf` package, CI included, keeps the NuttX default
  and needs no prefix at all, because that package is multilib and carries the `rv32imac` soft-float variant.
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
- Section headings in Markdown files, meaning `##` and `###`, must be in title case: "Build from Source" not "Build from source". Minor words stay
  lowercase unless they are the first word: the articles (a, an, the), the coordinating conjunctions (and, but, or, nor, so, yet, for), and the short
  prepositions (in, on, at, to, by, of, up, as, from, with, into, over). The prepositions are named because "from" has to be lowercase for "Build
  from Source" to be correct.
- Headings below that level, meaning `####` and deeper, use sentence case: "Clone the repository" not "Clone the Repository". Proper nouns keep their
  capitals, so "Build and run on the ESP32-P4" is correct.
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

1. The affected configuration builds from a clean tree inside the dev shell, after a `make nuttx-distclean`, not from an incremental one.
2. The image was flashed and seen running on the real board, and the notes say so.
3. Nothing under `external/` was modified, and no build output is staged. Check the nested HAL repository as well as the submodules themselves.

## Privacy

This repository is public, so anything committed is published.
The rules below are about everything that describes the person or the machine instead.

- No absolute paths. Use `PLAYGROUND_ROOT` for a path inside this repository, which the dev shell exports, and
  never write a home directory or a Nix store path into a saved configuration, a Makefile, or a document. Saved
  defconfigs get checked for this, since `savedefconfig` will happily record whatever was in `.config`.
- No hardware identifiers. Chip MAC addresses, eFuse contents, and USB serial numbers of adapters all identify a
  specific device. Read them at the console when needed and leave them there.
- No wireless survey data. Access point names from a scan identify a location, so record that `wapi scan` works
  and what it reports, not its output. Network names and pre-shared keys never belong in a file.
- No purchasing or personal notes. Shopping lists, prices, and plans go in a file matching `*.local.md`, which
  `.gitignore` covers with a generic pattern so the rule does not name what it hides.
- Selected console output only. Quote the lines that carry the technical point rather than pasting a whole
  capture, since a full boot log or shell session tends to carry identifiers nobody intended to publish.

Before committing, a scan of the tracked files outside `external/` for home directories, MAC addresses, and
network names costs a few seconds and is worth doing whenever a change touches documentation or saved
configurations.

## Commit Hygiene

- Keep commits scoped to one logical change, and keep a submodule version bump as its own commit.
- Say in the commit message which board the change was tested on, since behavior differs across chips.
