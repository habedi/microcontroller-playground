import importlib.util
import multiprocessing
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[3]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ppm = load_module("ppm2png", ROOT / "experiments/pico-face/tools/ppm2png.py")
hook = load_module("face_hook", ROOT / "tools/face-hook.py")
with patch.dict(sys.modules, {
    "serial": SimpleNamespace(SerialException=OSError, Serial=Mock()),
    "mcp.server.mcpserver": SimpleNamespace(MCPServer=Mock()),
}):
    board = load_module("board_mcp", ROOT / "tools/board-mcp.py")


class SerialTests(unittest.TestCase):
    def test_processes_wait_until_the_whole_conversation_closes(self):
        ctx = multiprocessing.get_context("fork")
        with tempfile.TemporaryDirectory() as directory:
            first_open = ctx.Event()
            second_lock = ctx.Event()
            second_open = ctx.Event()
            release = ctx.Event()
            closed = ctx.Event()
            results = ctx.Queue()

            def converse(first):
                class Port:
                    def __init__(self, port, baud, *, timeout, exclusive):
                        assert exclusive is True
                        if not first:
                            assert closed.is_set()
                        (first_open if first else second_open).set()
                        self.command = b""

                    def write(self, data):
                        if first and data == b"\r\n":
                            assert release.wait(5)
                        self.command = data.strip()

                    def flush(self): pass
                    def reset_input_buffer(self): pass
                    def read(self, size):
                        return self.command + b"\r\nreply\r\nnsh>"
                    def close(self):
                        if first: closed.set()

                flock = board.fcntl.flock

                def locking(fd, operation):
                    if not first: second_lock.set()
                    return flock(fd, operation)

                with patch.dict(os.environ, {"XDG_RUNTIME_DIR": directory}), \
                     patch.object(board, "PORT", directory + "/fake-port"), \
                     patch.object(board.serial, "Serial", Port), \
                     patch.object(board.fcntl, "flock", locking):
                    results.put(board.nsh("first" if first else "second"))

            processes = [ctx.Process(target=converse, args=(first,)) for first in (True, False)]
            try:
                processes[0].start()
                self.assertTrue(first_open.wait(5))
                processes[1].start()
                self.assertTrue(second_lock.wait(5))
                self.assertFalse(second_open.wait(0.1))
                release.set()
                for process in processes:
                    process.join(5)
                    self.assertEqual(process.exitcode, 0)
                self.assertEqual([results.get(timeout=2) for _ in processes], ["reply", "reply"])
            finally:
                release.set()
                for process in processes:
                    if process.is_alive():
                        process.terminate()
                        process.join(5)
                results.close()
                results.join_thread()

    def test_serial_open_failure_releases_lock_for_next_call(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(os.environ, {"XDG_RUNTIME_DIR": directory}), \
             patch.object(board, "PORT", directory + "/fake-port"), \
             patch.object(board, "_holder", return_value="terminal"):
            with patch.object(board.serial, "Serial", side_effect=OSError("busy")):
                self.assertIn("held by: terminal", board.nsh("first"))
            port = Mock()
            with patch.object(board.serial, "Serial", return_value=port) as opened, \
                 patch.object(board, "_read_until_prompt", side_effect=[("nsh>", True),
                               ("second\r\nreply\r\nnsh>", True)]):
                self.assertEqual(board.nsh("second"), "reply")
                opened.assert_called_once_with(board.PORT, board.BAUD, timeout=0.2, exclusive=True)
                port.close.assert_called_once()

    def test_exchange_error_closes_port_and_releases_lock(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(os.environ, {"XDG_RUNTIME_DIR": directory}), \
             patch.object(board, "PORT", directory + "/fake-port"):
            port = Mock()
            port.write.side_effect = OSError("disconnected")
            with patch.object(board.serial, "Serial", return_value=port):
                self.assertIn("disconnected", board.nsh("first"))
                port.close.assert_called_once()
            with patch.object(board, "_exchange", return_value="recovered"):
                self.assertEqual(board.nsh("second"), "recovered")


class PpmTests(unittest.TestCase):
    def read(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.ppm"
            path.write_bytes(data)
            return ppm.read_ppm(path)

    def test_comments_and_binary_pixels(self):
        pixels = b"\n #"
        self.assertEqual(
            self.read(b"P6\n# comment\n1 1\n255\n" + pixels),
            (1, 1, pixels),
        )

    def test_truncated_headers(self):
        for data in (b"", b"P6", b"P6\n1", b"P6\n# comment", b"P6\n1 1\n255"):
            with self.subTest(data=data), self.assertRaises(SystemExit):
                self.read(data)

    def test_invalid_format(self):
        for header in (b"P3\n1 1\n255\n", b"P6\n0 1\n255\n",
                       b"P6\n-1 1\n255\n", b"P6\nx 1\n255\n",
                       b"P6\n1 1\n65535\n", b"P6\n1 1\n15\n"):
            with self.subTest(header=header), self.assertRaises(SystemExit):
                self.read(header + b"abc")

    def test_truncated_pixels(self):
        with self.assertRaises(SystemExit):
            self.read(b"P6\n1 1\n255\nab")


class SpoolTests(unittest.TestCase):
    def test_failed_replace_keeps_previous_state_and_cleans_up(self):
        with tempfile.TemporaryDirectory() as directory:
            spool = Path(directory) / "state"
            spool.write_text("idle")
            with patch.object(hook, "SPOOL", str(spool)):
                with patch.object(hook.os, "replace", side_effect=OSError):
                    with self.assertRaises(OSError):
                        hook.spool("working")
            self.assertEqual(spool.read_text(), "idle")
            self.assertEqual(list(Path(directory).iterdir()), [spool])

    def test_overlapping_writers_publish_complete_words(self):
        replace = hook.os.replace
        with tempfile.TemporaryDirectory() as directory:
            spool = str(Path(directory) / "state")

            with patch.object(hook, "SPOOL", spool):
                with patch.object(hook.os, "replace") as mocked:
                    # Only the first writer pauses; the second publishes normally.
                    def first_replace(source, destination):
                        mocked.side_effect = replace
                        hook.spool("waiting")
                        replace(source, destination)

                    mocked.side_effect = first_replace
                    hook.spool("working")
                self.assertEqual(Path(spool).read_text(), "working")
                self.assertEqual(list(Path(directory).iterdir()), [Path(spool)])


if __name__ == "__main__":
    unittest.main()
