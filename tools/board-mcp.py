#!/usr/bin/env python3
"""MCP server that owns the board's serial console.

Every experiment in this repository ends up talking to a NuttShell prompt over
USB, and the port takes one reader at a time. A terminal left open blocks
everything else, and the failure looks like a hung board rather than a busy
port. This server is the single place that knows how to hold the port, sync the
prompt, and say who has it when it cannot.

It opens the port for one command and closes it again, so a terminal can still
be used between calls.

Tools:
    nsh         Run one NuttShell command and return its output.
    face        Set the expression on the LCD, starting the loop if needed.
    port_status Report whether the port is free and who holds it.

Output is scrubbed before it leaves this process. Chip MAC addresses identify a
specific device and wireless scan results identify a location, so neither is
returned. See the Privacy section of AGENTS.md.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time

import serial

from mcp.server.mcpserver import MCPServer

# The Pico 2 and the ESP32-P4 both appear as /dev/ttyACM0, so the port is
# configurable rather than assumed.
PORT = os.environ.get("BOARD_PORT", "/dev/ttyACM0")
BAUD = int(os.environ.get("BOARD_BAUD", "115200"))

PROMPT = "nsh>"

# Time allowed to see a prompt after nudging the console with a blank line.
SYNC_TIMEOUT_S = 3.0

FACE_STATES = ("idle", "working", "editing", "waiting", "failed", "done", "quit")

_ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]")
_MAC = re.compile(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b")


def _strip_ansi(text: str) -> str:
    return _ANSI.sub("", text)


def _scrub(text: str, command: str) -> str:
    """Removes anything that identifies the device or its surroundings."""
    text = _MAC.sub("<mac withheld>", text)

    # A scan lists the access points within radio range, which describes where
    # the board is. Report how many were seen and nothing else.
    if "wapi" in command and "scan" in command:
        lines = [line for line in text.splitlines() if line.strip()]
        header = next((l for l in lines if "bssid" in l.lower()), None)
        rows = [l for l in lines if header and l is not header and "/" not in l]
        summary = f"{len(rows)} access points seen, details withheld"
        return f"{header}\n{summary}" if header else summary

    return text


def _holder() -> str:
    """Names the process holding the port, for the error message."""
    if not shutil.which("fuser"):
        return "unknown (fuser is not installed)"

    try:
        done = subprocess.run(
            ["fuser", PORT], capture_output=True, text=True, timeout=5
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"

    pids = done.stdout.split() + done.stderr.replace(PORT + ":", "").split()
    pids = [p for p in pids if p.isdigit()]
    if not pids:
        return "nothing"

    names = []
    for pid in pids:
        try:
            with open(f"/proc/{pid}/comm", encoding="utf-8") as handle:
                names.append(f"{handle.read().strip()} (pid {pid})")
        except OSError:
            names.append(f"pid {pid}")

    return ", ".join(names)


def _read_until_prompt(port: serial.Serial, timeout_s: float) -> tuple[str, bool]:
    """Reads until the prompt comes back, rather than sleeping a fixed time."""
    deadline = time.monotonic() + timeout_s
    buf = bytearray()

    while time.monotonic() < deadline:
        chunk = port.read(512)
        if chunk:
            buf += chunk
            if _strip_ansi(buf.decode("utf-8", "replace")).rstrip().endswith(PROMPT):
                return buf.decode("utf-8", "replace"), True
        else:
            time.sleep(0.02)

    return buf.decode("utf-8", "replace"), False


def _clean(raw: str, command: str) -> str:
    """Drops the echoed command, the trailing prompt, and terminal escapes."""
    text = _strip_ansi(raw)
    lines = [line.rstrip() for line in text.splitlines()]

    if lines and lines[0].strip() == command.strip():
        lines = lines[1:]

    while lines and (not lines[-1].strip() or lines[-1].strip() == PROMPT):
        lines.pop()

    if lines and lines[-1].strip().endswith(PROMPT):
        lines[-1] = lines[-1].strip()[: -len(PROMPT)].rstrip()

    return "\n".join(line for line in lines if line.strip())


def _converse(command: str, timeout_s: float) -> str:
    try:
        port = serial.Serial(PORT, BAUD, timeout=0.2)
    except serial.SerialException as exc:
        return (
            f"cannot open {PORT}: {exc}\n"
            f"held by: {_holder()}\n"
            "Close the terminal on that port and try again."
        )
    except PermissionError:
        return (
            f"no permission to open {PORT}. Membership of the dialout group is "
            "needed on most distributions, and it takes a fresh login."
        )

    try:
        # Nudge the console and wait for a prompt, so a command is never sent
        # into a board that is still booting.
        port.write(b"\r\n")
        port.flush()
        _, ready = _read_until_prompt(port, SYNC_TIMEOUT_S)
        if not ready:
            return (
                f"{PORT} opened but no prompt appeared within {SYNC_TIMEOUT_S:g}s. "
                "The board may be booting, held at a fault, or running something "
                "that does not return to the shell."
            )

        port.reset_input_buffer()
        port.write(command.encode() + b"\r\n")
        port.flush()

        raw, ready = _read_until_prompt(port, timeout_s)
        out = _scrub(_clean(raw, command), command)

        if not ready:
            note = f"(no prompt within {timeout_s:g}s, output may be incomplete)"
            return f"{out}\n{note}" if out else note

        return out or "(no output)"
    finally:
        port.close()


def nsh(command: str, timeout_s: float = 8.0) -> str:
    """Run one NuttShell command on the board and return its output.

    Args:
        command: The command line to run, for example "df -h" or "free".
        timeout_s: How long to wait for the prompt to come back.
    """
    if not command.strip():
        return "give a command to run"

    return _converse(command, timeout_s)


def face(state: str) -> str:
    """Set the expression on the LCD.

    Args:
        state: One of idle, working, editing, waiting, failed, done, or quit.
    """
    state = state.strip().lower()
    if state not in FACE_STATES:
        return f"unknown state {state!r}. Use one of: {', '.join(FACE_STATES)}"

    if state != "quit":
        # Start the render loop if it is not already running, so the first call
        # of a session does something visible.
        running = _converse("ps", 8.0)
        if "face" not in running:
            started = _converse("face &", 8.0)
            if "not found" in started:
                return (
                    "the face application is not in this image. Build the "
                    "configuration that sets CONFIG_PICO_FACE."
                )

    out = _converse(f"face {state}", 8.0)

    # A successful change prints nothing, so say what happened rather than
    # reporting the absence of output.
    if not out or out == "(no output)":
        return f"face is now {state}"

    return out


def port_status() -> str:
    """Report whether the board's serial port is free, and who holds it."""
    if not os.path.exists(PORT):
        return f"{PORT} does not exist. Is the board plugged in?"

    holder = _holder()
    if holder != "nothing":
        return f"{PORT} is held by {holder}"

    reply = _converse("uname -a", 6.0)
    return f"{PORT} is free\n{reply}"


def _selftest() -> int:
    """Exercises the tools without an MCP client, for verification by hand."""
    print("port_status:")
    print("  " + port_status().replace("\n", "\n  "))
    print("nsh uptime:")
    print("  " + nsh("uptime").replace("\n", "\n  "))
    print("nsh df -h:")
    print("  " + nsh("df -h").replace("\n", "\n  "))
    print("face working:")
    print("  " + face("working").replace("\n", "\n  "))
    return 0


def main() -> int:
    if "--selftest" in sys.argv:
        return _selftest()

    server = MCPServer(
        name="board",
        instructions=(
            "Talks to the NuttX board over its USB serial console. Use nsh to "
            "run a shell command, face to change the expression on the LCD, "
            "and port_status when a call reports the port is busy. MAC "
            "addresses and wireless scan details are withheld on purpose."
        ),
    )

    for tool in (nsh, face, port_status):
        server.tool()(tool)

    server.run(transport="stdio")
    return 0


if __name__ == "__main__":
    sys.exit(main())
