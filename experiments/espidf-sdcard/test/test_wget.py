from pathlib import Path
import stat
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
                     "fat-backup-error", "fat-staging-create-error", "fat-publish-error",
                     "fat-open-error", "fat-stat-error", "fat-close-error"):
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
        staging = list(self.directory.glob("download.part*"))
        self.assertEqual(len(staging), 1)
        backup = staging[0] / "old"
        self.assertEqual(backup.read_bytes(), b"previous")
        self.assertIn(str(backup).encode(), result.stderr)
        self.assertEqual(list(staging[0].iterdir()), [backup])
        self.assertEqual(list(self.directory.iterdir()), staging)

    def test_destination_swapped_before_backup_is_restored_without_publication(self):
        for kind in ("directory", "symlink", "fifo"):
            with self.subTest(kind=kind):
                self.output.write_bytes(b"previous")
                result = self.invoke("fat-swap-" + kind)
                self.assertEqual(result.returncode, 1, result.stderr)
                displaced = self.directory / "download.displaced"
                self.assertEqual(displaced.read_bytes(), b"previous")
                if kind == "directory":
                    self.assertTrue(self.output.is_dir())
                    self.output.rmdir()
                elif kind == "symlink":
                    self.assertTrue(self.output.is_symlink())
                    self.assertEqual(self.output.read_bytes(), b"previous")
                    self.output.unlink()
                else:
                    self.assertTrue(stat.S_ISFIFO(self.output.stat().st_mode))
                    self.output.unlink()
                self.assertEqual(list(self.directory.iterdir()), [displaced])
                displaced.unlink()

    def test_competing_destination_keeps_both_outputs(self):
        self.output.write_bytes(b"previous")
        result = self.invoke("fat-competitor")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(self.output.read_bytes(), b"competing output")
        staging = list(self.directory.glob("download.part*"))
        self.assertEqual(len(staging), 1)
        backup = staging[0] / "old"
        self.assertEqual(backup.read_bytes(), b"previous")
        self.assertIn(str(backup).encode(), result.stderr)
        self.assertEqual(list(staging[0].iterdir()), [backup])

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

    def test_staging_name_collision_does_not_touch_existing_directory(self):
        result = self.invoke("staging-collision")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), b"abc\0def")
        existing = list(self.directory.glob("download.part*"))
        self.assertEqual(len(existing), 1)
        self.assertEqual((existing[0] / "keep").read_bytes(), b"another download")

    def test_staging_collisions_stop_without_changing_destination(self):
        self.output.write_bytes(b"previous")
        result = self.invoke("staging-collisions-exhausted")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(self.output.read_bytes(), b"previous")
        self.assertEqual(list(self.directory.iterdir()), [self.output])

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
