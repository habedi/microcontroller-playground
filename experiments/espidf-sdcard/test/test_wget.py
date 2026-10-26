from pathlib import Path
import subprocess
import tempfile
import unittest


BINARY = Path(__file__).parent / "build/test_wget"


class WgetTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.output = self.directory / "download"

    def invoke(self, mode="success", url="https://example.test/file", path=None):
        return subprocess.run([str(BINARY), mode, url, str(path or self.output)],
                              capture_output=True)

    def test_binary_download_and_redirect_body(self):
        for mode in ("success", "redirect", "fat"):
            with self.subTest(mode=mode):
                self.output.write_bytes(b"previous")
                result = self.invoke(mode)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.output.read_bytes(), b"abc\0def")
                self.assertEqual(list(self.directory.iterdir()), [self.output])

    def test_failures_preserve_destination_and_remove_temporary(self):
        for mode in ("init-error", "network-error", "http-error", "incomplete",
                     "write-error", "close-error", "rename-error",
                     "fat-backup-error", "fat-backup-create-error", "fat-publish-error"):
            with self.subTest(mode=mode):
                self.output.write_bytes(b"previous")
                result = self.invoke(mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(self.output.read_bytes(), b"previous")
                self.assertEqual(list(self.directory.iterdir()), [self.output])

    def test_failed_rollback_keeps_backup_and_reports_its_path(self):
        self.output.write_bytes(b"previous")
        result = self.invoke("fat-rollback-error")
        self.assertEqual(result.returncode, 1, result.stderr)
        backups = list(self.directory.glob("download.bak*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), b"previous")
        self.assertIn(str(backups[0]).encode(), result.stderr)
        self.assertEqual(list(self.directory.iterdir()), backups)

    def test_fat_directory_is_never_replaced(self):
        self.output.mkdir()
        (self.output / "keep").write_bytes(b"previous")
        result = self.invoke("fat")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual((self.output / "keep").read_bytes(), b"previous")
        self.assertEqual(list(self.directory.iterdir()), [self.output])

    def test_failure_without_existing_destination(self):
        self.assertEqual(self.invoke("network-error").returncode, 1)
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_stdout_contains_only_response_body(self):
        result = self.invoke(path="-")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"abc\0def")

    def test_empty_response(self):
        result = self.invoke("empty")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), b"")

    def test_bad_url_or_output_path(self):
        self.assertEqual(self.invoke(url="file:///data/private").returncode, 1)
        self.assertEqual(self.invoke(path=self.directory / "missing/file").returncode, 1)
        self.assertEqual(self.invoke(path="x" * 600).returncode, 1)
        self.assertEqual(list(self.directory.iterdir()), [])
