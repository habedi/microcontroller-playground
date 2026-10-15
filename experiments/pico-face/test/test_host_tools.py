import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ppm = load_module("ppm2png", ROOT / "experiments/pico-face/tools/ppm2png.py")
hook = load_module("face_hook", ROOT / "tools/face-hook.py")


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
