#!/usr/bin/env python3
"""Transfer files to Apache NuttX /data using Zmodem over USB serial."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
import serial

DEFAULT_PORT = os.environ.get("BOARD_PORT", "/dev/ttyACM0")
DEFAULT_BAUD = int(os.environ.get("BOARD_BAUD", "115200"))
PROMPT = "nsh>"


def sync_prompt(ser: serial.Serial, timeout_s: float = 3.0) -> bool:
    """Nudges the console with newlines until the prompt appears."""
    deadline = time.monotonic() + timeout_s
    ser.reset_input_buffer()
    ser.write(b"\r\n")

    buf = ""
    while time.monotonic() < deadline:
        chunk = ser.read(ser.in_waiting or 1).decode("utf-8", "replace")
        if chunk:
            buf += chunk
            if PROMPT in buf:
                return True
        time.sleep(0.05)
    return False


def send_file(port: str, baud: int, file_path: str, target_dir: str = "/data") -> None:
    if not os.path.isfile(file_path):
        print(f"File not found: {file_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Connecting to {port} at {baud} baud...")
    with serial.Serial(port, baudrate=baud, timeout=1.0) as ser:
        if not sync_prompt(ser):
            print("Failed to reach NuttShell prompt. Is the board running?", file=sys.stderr)
            sys.exit(1)

        print(f"Starting Zmodem receiver on board into {target_dir}...")
        cmd = f"rz -p {target_dir}\r\n" if target_dir else "rz\r\n"
        ser.write(cmd.encode("utf-8"))
        time.sleep(0.2)

    # Port is now closed in Python so sz can open it
    print(f"Sending {file_path} via sz...")
    with open(port, "rb+", buffering=0) as tty:
        proc = subprocess.run(
            ["sz", "-b", "--quiet", file_path],
            stdin=tty,
            stdout=tty,
            stderr=subprocess.PIPE,
            text=True,
            timeout=30,
        )
        if proc.returncode != 0:
            print(f"sz failed: {proc.stderr}", file=sys.stderr)
            sys.exit(proc.returncode)

    print("Transfer complete. Verifying on board...")
    time.sleep(0.5)
    with serial.Serial(port, baudrate=baud, timeout=1.0) as ser:
        if sync_prompt(ser):
            filename = os.path.basename(file_path)
            ser.write(f"ls -l {target_dir}/{filename}\r\n".encode("utf-8"))
            time.sleep(0.3)
            out = ser.read_all().decode("utf-8", "replace")
            print("Board response:")
            print(out.strip())


def main() -> None:
    parser = argparse.ArgumentParser(description="Transfer files to NuttX /data using Zmodem.")
    parser.add_argument("file", help="Path to file to transfer (e.g. main.wasm)")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port (default: %(default)s)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate (default: %(default)s)")
    parser.add_argument("--dir", default="/data", help="Target directory on board (default: %(default)s)")
    args = parser.parse_args()

    send_file(args.port, args.baud, args.file, args.dir)


if __name__ == "__main__":
    main()
