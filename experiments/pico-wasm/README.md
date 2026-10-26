## WebAssembly Execution on RP2350

The custom NuttX configuration and board patches were removed from the repository.
The results below describe the previously tested firmware; a fresh checkout cannot reproduce it directly.

This experiment compiles a C program to a standalone WebAssembly module (`main.wasm`), transfers it to the
Raspberry Pi Pico 2 internal flash storage (`/data`) over the serial console using Zmodem without reflashing,
and executes it using WAMR (`iwasm`).

### Board and OS

- Board: Raspberry Pi Pico 2 WH (RP2350, Arm Cortex-M33).
- OS: Apache NuttX (configuration `usbnsh-wamr`).

### Build and Flash the Firmware

This firmware used the historical `usbnsh-wamr` configuration and custom Pico board patches.
Restore them from Git history before rebuilding or flashing.

Connect to the NuttShell:

```shell
make console TTY=/dev/ttyACM0
```

Verify that `/data` is mounted and `iwasm` and `rz` are available:

```text
nsh> df -h
Filesystem     Size   Used  Available Mounted on
/dev/rpflash     3M    12K         3M /data
tmpfs          500K     0K       500K /tmp
romfs          384B   384B         0B /etc
procfs           0B     0B         0B /proc

nsh> which iwasm rz
/bin/iwasm
/bin/rz
```

### Compile the WebAssembly Module

Build `main.wasm` from `main.c` using the dev shell's Zig compiler:

```shell
cd experiments/pico-wasm
make
```

The resulting `main.wasm` is 180 bytes and exports `main`, `fib`, and `add`.

The Makefile sets `-Wl,-z,stack-size=4096 -Wl,--initial-memory=65536 -Wl,--max-memory=131072` so the module
requests 1 page (64 KB) initial memory instead of the `wasm-ld` default of 16 pages (1 MB), which would
exceed the RP2350's 512 KB SRAM and fail with `allocate linear memory failed`.

### Transfer Without Reflashing

#### Option 1: Automated script

Run the Python helper from the repository root:

```shell
uv run python experiments/pico-wasm/send.py experiments/pico-wasm/main.wasm --port /dev/ttyACM0
```

#### Option 2: Manual transfer via picocom

1. In the `picocom` session, start the receiver:
   ```text
   nsh> rz
   ```
2. Press `Ctrl-A` then `Ctrl-S`.
3. When prompted, enter:
   ```text
   *** file: experiments/pico-wasm/main.wasm
   ```
4. The file is saved to `/data/main.wasm`.

### Run the WebAssembly Module

Run the default `main()` function:

```text
nsh> iwasm /data/main.wasm
```

```text
nsh> iwasm -f add /data/main.wasm 15 27
0x2a:i32

nsh> iwasm -f fib /data/main.wasm 10
0x37:i32
```

`0x2a` is 42 (15 + 27), and `0x37` is 55 (`fib(10)`).
