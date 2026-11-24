"""Release packaging helper: Windows zip layout and refusals."""

import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "ci" / "pack_windows.py"


class WindowsPackagingTests(unittest.TestCase):
    def pack(self, cwd: Path, name: str):
        return subprocess.run([sys.executable, str(SCRIPT), name],
                              cwd=cwd, capture_output=True, text=True)

    def test_archive_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = Path(tmp) / "dist"
            (dist / "rvm-1.2.3-windows-x64" / "bin").mkdir(parents=True)
            (dist / "rvm-1.2.3-windows-x64" / "bin" / "rvm.exe").write_bytes(b"bin")
            (dist / "rvm-1.2.3-windows-x64" / "README.md").write_text("readme", encoding="utf-8")

            result = self.pack(tmp, "rvm-1.2.3-windows-x64")

            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(dist / "rvm-1.2.3-windows-x64.zip") as archive:
                self.assertEqual(sorted(archive.namelist()), [
                    "rvm-1.2.3-windows-x64/README.md",
                    "rvm-1.2.3-windows-x64/bin/rvm.exe",
                ])

    def test_missing_package_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.pack(tmp, "rvm-9.9.9")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing package directory", result.stderr)


if __name__ == "__main__":
    unittest.main()
