#!/usr/bin/env python3
"""Flash official native Espressif SDIO coprocessor firmware to ESP32-C6."""

import subprocess
import sys
import time

try:
    import serial
except ImportError:
    serial = None

def prepare_c6_download_mode():
    if not serial:
        return
    print("Pulsing P4 to latch C6 into download mode...")
    try:
        s = serial.Serial("/dev/ttyACM0", 115200)
        s.dtr = False
        s.rts = True
        time.sleep(0.1)
        s.rts = False
        s.close()
        time.sleep(1.0)
    except Exception as e:
        print(f"Note: P4 reset pulse skipped: {e}")

def main():
    prepare_c6_download_mode()
    cmd = [
        "uv", "run", "esptool",
        "--chip", "esp32c6",
        "-p", "/dev/ttyUSB0",
        "-b", "230400",
        "--before", "no-reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "4MB",
        "0x0", "build/c6-coprocessor/bootloader/bootloader.bin",
        "0x8000", "build/c6-coprocessor/partition_table/partition-table.bin",
        "0xd000", "build/c6-coprocessor/ota_data_initial.bin",
        "0x10000", "build/c6-coprocessor/eh_cp_wifi_scan.bin"
    ]
    print("Flashing official Espressif esp_hosted SDIO firmware...")
    ret = subprocess.run(cmd)
    return ret.returncode

if __name__ == "__main__":
    sys.exit(main())
