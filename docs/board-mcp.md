## Serial Console over MCP

`tools/board-mcp.py` is a small MCP server that owns the board's USB serial
console, so a coding agent can run NuttShell commands without a shell script
per question and without fighting a terminal for the port.

The problem it solves is narrow and real. The port takes one reader at a time,
and when a terminal already has it, the failure looks like a hung board: the
command goes nowhere and nothing comes back. This server always says which
process holds the port instead.

### What It Exposes

| Tool | Purpose |
| ---- | ------- |
| `nsh` | Runs one NuttShell command and returns its output. |
| `face` | Sets the expression on the LCD, starting the render loop if needed. |
| `port_status` | Says whether the port is free, and names the holder if not. |

### Setup

`.mcp.json` in the repository root registers it, so Claude Code picks it up on
its own. The port defaults to `/dev/ttyACM0` and comes from `BOARD_PORT`, which
matters because the Pico 2 and the ESP32-P4 claim the same device name.

Check it by hand without an MCP client:

```shell
uv run --no-project python tools/board-mcp.py --selftest
```

The `mcp` package is a dependency in `pyproject.toml`. It pulls in a web server
stack that a stdio server never uses, which is worth knowing before adding more
of these.

### Waiting for the Prompt

The server nudges the console with a blank line, waits for `nsh>`, then sends
the command and reads until the prompt returns. That is the part worth copying
into any other script that talks to this board. Sleeping a fixed time instead
either truncates a slow command or wastes seconds on a fast one, and it cannot
tell a slow write from a board that has stopped answering.

It opens the port for one command and closes it again, so a terminal can still
be used between calls.

### What It Withholds

Output is scrubbed inside the server rather than left to the caller's judgment,
which puts the Privacy rules in AGENTS.md into code:

- MAC addresses are replaced, since they identify a specific device.
- A `wapi scan` is reduced to how many access points were seen, since the names
  identify a location.

Nothing else is filtered. eFuse contents and USB serial numbers would come
through, so a command that reads them still needs care.

### Limits

- No flashing. Getting into BOOTSEL mode needs a physical button press on this
  board, so a flash tool could not finish the job anyway.
- One command per call. There is no session, so `cd` does not persist.
- The scrubbing is pattern matching, not understanding.

### Verified

Against the Pico 2 WH running the `usbnsh-lcd-wifi` image:

- The protocol handshake and `tools/list` return the three tools.
- `nsh` returns output from `uname -a`, `uptime`, `df -h`, and `ifconfig`, with
  the MAC address withheld.
- `face` starts the render loop when it is not running, and `/tmp/face` confirms
  the state afterwards.
- A `wapi scan` is summarised, and the count was checked against the unscrubbed
  output.
