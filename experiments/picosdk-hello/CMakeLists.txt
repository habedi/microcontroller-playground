cmake_minimum_required(VERSION 3.13)

# The Nix dev shell exports PICO_SDK_PATH; see flake.nix.
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picosdk_hello C CXX ASM)
pico_sdk_init()

add_executable(hello main.c)

# The console goes over the USB cable, because the Pico 2 W has no LED on a
# GPIO pin and no UART adapter is needed this way.
pico_enable_stdio_usb(hello 1)
pico_enable_stdio_uart(hello 0)

target_link_libraries(hello pico_stdlib)
pico_add_extra_outputs(hello)
