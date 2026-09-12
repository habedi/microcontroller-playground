#!/usr/bin/env python3
"""Claude Code hook that puts the session's state on the board's LCD.

A hook runs in front of the thing it reports on, so it has to be quick. A
serial round trip to the board takes a second or two, which would be added to
every tool call. So this script does two jobs:

Called by a hook, it reads the event from standard input, works out which
expression it means, writes that word to a spool file, starts a detached copy
of itself to do the slow part, and exits. That path touches no serial port and
imports nothing heavy.

Called with --push, it takes the lock, reads the spool file, and sends the word
to the board. If another copy already holds the lock it exits: the copy that
has it will read whatever the newest word is, so the last event wins and a
backlog cannot build up.

Nothing is ever printed and the exit status is always zero, because a hook that
complains gets in the way of the session it is meant to describe.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

# Deliberately not tempfile.gettempdir(), which follows TMPDIR. The hook and
# the pusher have to agree on this path, and TMPDIR is not the same in every
# context a hook runs from.
_RUNTIME = os.environ.get("XDG_RUNTIME_DIR") or "/tmp"

SPOOL = os.path.join(_RUNTIME, "claude-face.state")
LOCK = os.path.join(_RUNTIME, "claude-face.lock")

# Tools that change files on disk, which the face reports as looking down.
EDIT_TOOLS = {
    "Edit",
    "Write",
    "NotebookEdit",
    "MultiEdit",
    "write_to_file",
    "replace_file_content",
}


def _extract_tool_name(event: dict) -> str:
    """Extracts the tool name from either Claude Code or Antigravity payloads."""
    name = event.get("tool_name")
    if not name:
        tool_call = event.get("toolCall")
        if isinstance(tool_call, dict):
            name = tool_call.get("name")
    if not name:
        return ""
    # Strip any plugin/namespace prefix such as "default_api:run_command".
    return name.split(":")[-1]


def word_for(event: dict, event_name: str | None = None) -> str | None:
    """Maps a hook event to one of the six expressions, or None to do nothing."""
    name = event_name or event.get("hook_event_name", "")

    if name in ("SessionStart", "SessionEnd"):
        return "idle"

    if name == "PreToolUse":
        tool = _extract_tool_name(event)
        if tool in EDIT_TOOLS:
            return "editing"
        if tool == "ask_question":
            return "waiting"
        return "working"

    if name == "PostToolUse":
        # Antigravity reports tool errors in event["error"].
        if event.get("error"):
            return "failed"

        # Claude Code reports tool errors in event["tool_response"].
        response = event.get("tool_response")
        if isinstance(response, dict):
            if response.get("is_error") or response.get("error"):
                return "failed"
        elif isinstance(response, str) and response.lower().startswith("error"):
            return "failed"

        return None

    if name == "PreInvocation":
        return "working"

    if name == "Notification":
        return "waiting"

    if name in ("Stop", "SubagentStop", "PostInvocation"):
        return "done"

    return None


def spool(word: str) -> None:
    tmp = SPOOL + ".new"
    with open(tmp, "w", encoding="utf-8") as handle:
        handle.write(word)

    # Replace atomically, so the pusher never reads a half-written word.
    os.replace(tmp, SPOOL)


def _python_bin() -> str:
    """Finds the Python interpreter that has project dependencies installed."""
    here = os.path.dirname(os.path.abspath(__file__))
    venv_py = os.path.join(os.path.dirname(here), ".venv", "bin", "python")
    if os.path.exists(venv_py):
        return venv_py
    return sys.executable


def detach() -> None:
    """Starts the pushing half without waiting for it."""
    subprocess.Popen(
        [_python_bin(), os.path.abspath(__file__), "--push"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )


def push() -> None:
    import fcntl

    handle = open(LOCK, "w", encoding="utf-8")
    try:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        # Someone else is talking to the board and will pick up the newest
        # word from the spool file. Dropping this one costs nothing.
        return

    try:
        import importlib.util

        here = os.path.dirname(os.path.abspath(__file__))
        spec = importlib.util.spec_from_file_location(
            "board_mcp", os.path.join(here, "board-mcp.py")
        )
        board = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(board)

        if not os.path.exists(board.PORT):
            return

        # A hook that fired while this copy held the lock gave up and left
        # its word in the spool file, so keep going until the word read
        # before a push is still the word there after it.
        sent = None
        while True:
            with open(SPOOL, encoding="utf-8") as spooled:
                word = spooled.read().strip()

            if not word or word == sent:
                return

            # face() starts the render loop when it is not running, which is
            # what makes the first event of a session do something visible.
            board.face(word)
            sent = word
    finally:
        fcntl.flock(handle, fcntl.LOCK_UN)
        handle.close()


def main() -> int:
    try:
        if "--push" in sys.argv:
            push()
            return 0

        event_name = None
        if "--event" in sys.argv:
            idx = sys.argv.index("--event")
            if idx + 1 < len(sys.argv):
                event_name = sys.argv[idx + 1]

        event = {}
        try:
            event = json.load(sys.stdin)
        except Exception:
            pass

        word = word_for(event, event_name=event_name)

        with open(os.path.join(_RUNTIME, "face-hook.log"), "a", encoding="utf-8") as log:
            log.write(f"event={event_name} tool={_extract_tool_name(event)} word={word}\n")

        if word is not None:
            spool(word)
            detach()

        # If invoked under Antigravity, output the expected JSON response on stdout.
        name = event_name or event.get("hook_event_name", "")
        is_antigravity = (
            event_name is not None
            or "toolCall" in event
            or "conversationId" in event
            or "stepIdx" in event
        )
        if is_antigravity:
            if name == "PreToolUse":
                print(json.dumps({"decision": "allow"}))
            elif name == "Stop":
                print(json.dumps({"decision": "approve"}))
            else:
                print(json.dumps({}))
    except Exception:
        # A hook must never break the session it is reporting on.
        pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
