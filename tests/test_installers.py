"""Installer/uninstaller contract tests.

Windows exercises install.ps1 + uninstall.ps1 (including the registered
uninstall entry, under a throwaway registry key). Unix exercises install.sh.
Both use a local HTTP server that mimics a release, so no network is needed.
"""

import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import unittest
import zipfile
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.1.0"
BIN = ROOT / "target" / "Release" / ("rvm.exe" if os.name == "nt" else "rvm")


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64", "x64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    raise unittest.SkipTest(f"unsupported test arch: {machine}")


def host_platform() -> str:
    if os.name == "nt":
        return "windows"
    return "macos" if platform.system() == "Darwin" else "linux"


PLATFORM = host_platform()
ARCH = host_arch()
ASSET = f"rvm-{VERSION}-{PLATFORM}-{ARCH}." + ("zip" if PLATFORM == "windows" else "tar.gz")
PAYLOAD = f"rvm-{VERSION}-{PLATFORM}-{ARCH}"


def build_release(site: Path) -> None:
    release = site / f"v{VERSION}"
    release.mkdir(parents=True)
    asset = release / ASSET
    if PLATFORM == "windows":
        with zipfile.ZipFile(asset, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.write(BIN, f"{PAYLOAD}/bin/rvm.exe")
            archive.write(ROOT / "packaging" / "uninstall.ps1", f"{PAYLOAD}/uninstall.ps1")
            archive.write(ROOT / "README.md", f"{PAYLOAD}/README.md")
            archive.write(ROOT / "README.en.md", f"{PAYLOAD}/README.en.md")
            archive.write(ROOT / "LICENSE", f"{PAYLOAD}/LICENSE")
    else:
        with tarfile.open(asset, "w:gz") as archive:
            for source, name, mode in (
                (BIN, f"{PAYLOAD}/bin/rvm", 0o755),
                (ROOT / "packaging" / "install.sh", f"{PAYLOAD}/install.sh", 0o644),
                (ROOT / "README.md", f"{PAYLOAD}/README.md", 0o644),
                (ROOT / "README.en.md", f"{PAYLOAD}/README.en.md", 0o644),
                (ROOT / "LICENSE", f"{PAYLOAD}/LICENSE", 0o644),
            ):
                info = tarfile.TarInfo(name)
                info.size = source.stat().st_size
                info.mode = mode
                with source.open("rb") as stream:
                    archive.addfile(info, stream)
    digest = hashlib.sha256(asset.read_bytes()).hexdigest()
    (release / "SHA256SUMS").write_text(f"{digest}  {ASSET}\n", encoding="utf-8")


@unittest.skipUnless(BIN.is_file(), f"rvm binary not built: {BIN}")
class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.site = Path(tempfile.mkdtemp(prefix="rvm-installer-site-"))
        build_release(cls.site)
        handler = partial(SimpleHTTPRequestHandler, directory=str(cls.site))
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        shutil.rmtree(cls.site, ignore_errors=True)

    def env(self):
        env = dict(os.environ)
        env["RVM_DOWNLOAD_BASE"] = self.base
        return env

    def test_install_and_uninstall(self):
        work = Path(tempfile.mkdtemp(prefix="rvm-installer-home-"))
        self.addCleanup(shutil.rmtree, work, True)
        if PLATFORM == "windows":
            self._windows(work)
        else:
            self._unix(work)

    def _windows(self, work: Path) -> None:
        import winreg

        install_dir = work / "rvm"
        key_path = rf"Software\RingAire\rvm-test-{os.getpid()}"
        env = self.env()
        env["RVM_UNINSTALL_KEY"] = rf"HKCU:\{key_path}"
        powershell = shutil.which("powershell") or shutil.which("pwsh")
        self.assertIsNotNone(powershell, "PowerShell is required for the Windows installer test")

        install = subprocess.run(
            [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             str(ROOT / "packaging" / "install.ps1"),
             "-Version", VERSION, "-InstallDir", str(install_dir), "-NoPath"],
            env=env, capture_output=True, text=True, timeout=180)
        self.assertEqual(install.returncode, 0, install.stdout + install.stderr)
        self.assertTrue((install_dir / "rvm.exe").is_file())
        self.assertTrue((install_dir / "uninstall.ps1").is_file())
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
            self.assertEqual(winreg.QueryValueEx(key, "DisplayVersion")[0], VERSION)
            self.assertEqual(winreg.QueryValueEx(key, "Publisher")[0], "RingAire")

        uninstall = subprocess.run(
            [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             str(install_dir / "uninstall.ps1"), "-Yes"],
            env=env, capture_output=True, text=True, timeout=180)
        self.assertEqual(uninstall.returncode, 0, uninstall.stdout + uninstall.stderr)
        deadline = time.time() + 15
        while install_dir.exists() and time.time() < deadline:
            time.sleep(0.5)
        self.assertFalse(install_dir.exists(), "installation directory survived uninstall")
        try:
            winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path)
        except FileNotFoundError:
            pass
        else:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key_path)
            self.fail("uninstall entry was not removed")

    def _unix(self, work: Path) -> None:
        prefix = work / "prefix"
        install = subprocess.run(
            ["sh", str(ROOT / "packaging" / "install.sh"),
             "--version", VERSION, "--prefix", str(prefix)],
            env=self.env(), capture_output=True, text=True, timeout=180)
        self.assertEqual(install.returncode, 0, install.stdout + install.stderr)
        binary = prefix / "bin" / "rvm"
        self.assertTrue(binary.is_file())
        self.assertTrue(os.access(binary, os.X_OK))
        version = subprocess.run([str(binary), "version"], capture_output=True, text=True)
        self.assertEqual(version.returncode, 0)

        uninstall = subprocess.run(
            ["sh", str(ROOT / "packaging" / "install.sh"),
             "--uninstall", "--prefix", str(prefix)],
            env=self.env(), capture_output=True, text=True, timeout=60)
        self.assertEqual(uninstall.returncode, 0, uninstall.stdout + uninstall.stderr)
        self.assertFalse(binary.exists())


if __name__ == "__main__":
    unittest.main()
