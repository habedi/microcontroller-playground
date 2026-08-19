## Pico SDK Hello

Smallest Pico SDK program that proves the second SDK builds and that its USB console works.

- Board: Raspberry Pi Pico 2 WH
- OS: none, bare metal on the Pico SDK
- Result: builds, 57 KB UF2. Not yet run on hardware.

### Build

```shell
make picosdk-configure
make picosdk-build
```

`PICOSDK_SRC` selects the project directory and defaults to this one. `PICOSDK_BOARD` defaults to `pico2_w`,
which is the SDK board definition matching the Pico 2 WH. Output goes to
`build/picosdk/picosdk-hello/hello.uf2`, outside the SDK submodule.

### Flash and Connect

Hold BOOTSEL while plugging the board in, then:

```shell
make flash-pico-uf2 UF2=build/picosdk/picosdk-hello/hello.uf2
make console TTY=/dev/ttyACM0
```

The program prints a counter once per second over the USB serial console. Unlike NuttX, there is no shell and
no second Enter to press; output starts on its own.

### What the SDK Brings

CMake reports these during configuration, all from submodules under `external/pico-sdk/lib`:

- TinyUSB, for USB device and host.
- BTstack, with "Pico W Bluetooth build support available". This covers Classic Bluetooth as well as LE, which
  is what connecting an existing Bluetooth keyboard, mouse, or controller needs.
- cyw43-driver and lwIP, with "Pico W Wi-Fi build support available".
- mbedtls, for TLS.

`make submodules` fetches all of them, since they are submodules of the pico-sdk submodule.

### Why This Exists Alongside NuttX

NuttX gives a shell, a filesystem, and a driver model. The Pico SDK gives vendor-tested wireless, Bluetooth,
and USB stacks, and a much smaller image. The wireless work under NuttX needed a board patch, a firmware blob
converted from a C header, and two guards against silent failures, and it still has an intermittent cold boot.
The same functionality here is a library that Raspberry Pi test.

Zig can link against these C libraries, so an emulator core written in Zig can use the SDK for display, input,
and USB without porting drivers. Zig cannot target the ESP32-S3, because Xtensa is not in upstream LLVM, so
the Pico 2 is the Zig platform.
