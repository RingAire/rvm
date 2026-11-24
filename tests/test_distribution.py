"""Distribution-contract tests for rvm.

The binary must exist first (`make -C tools/rvm`). Tests start a local HTTP
server that mimics the published dist tree, so they never touch the network:

    <site>/index.txt
    <site>/v<version>/ringecho-<version>-<platform>-<arch>.{zip|tar.gz}

Build output layout (checked by install):
    ~/.rvm/versions/<version>/ringecho-<version>-<platform>-<arch>/bin/...
"""

import io
import os
import platform
import shutil
import subprocess
import tarfile
import tempfile
import threading
import unittest
import zipfile
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ".exe" if os.name == "nt" else ""
DEFAULT_BIN = ROOT / "target" / "Release" / f"rvm{EXE}"
RVM = Path(os.environ.get("RVM_BIN", DEFAULT_BIN))
VERSION = "0.1.0"
TEST_ROOT = os.environ.get("RVM_TEST_ROOT")


def make_temp(prefix: str) -> Path:
    """Temporary dir; RVM_TEST_ROOT relocates it for sandboxed runs."""
    if TEST_ROOT:
        Path(TEST_ROOT).mkdir(parents=True, exist_ok=True)
        return Path(tempfile.mkdtemp(prefix=prefix, dir=TEST_ROOT))
    return Path(tempfile.mkdtemp(prefix=prefix))


def host_platform() -> str:
    if os.name == "nt":
        return "windows"
    if platform.system() == "Darwin":
        return "macos"
    return "linux"


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64", "x64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    raise unittest.SkipTest(f"unsupported test arch: {machine}")


PLATFORM = host_platform()
ARCH = host_arch()
PAYLOAD = f"ringecho-{VERSION}-{PLATFORM}-{ARCH}"
SUFFIX = ".zip" if os.name == "nt" else ".tar.gz"


def write_asset(site: Path, version: str) -> Path:
    payload = f"ringecho-{version}-{PLATFORM}-{ARCH}"
    asset = site / f"v{version}" / f"{payload}{SUFFIX}"
    asset.parent.mkdir(parents=True, exist_ok=True)
    members = [f"{payload}/bin/rev{EXE}", f"{payload}/bin/rem{EXE}",
               f"{payload}/bin/rvm{EXE}"]
    if SUFFIX == ".zip":
        with zipfile.ZipFile(asset, "w") as archive:
            for name in members:
                archive.writestr(name, b"fixture")
    else:
        with tarfile.open(asset, "w:gz") as archive:
            for name in members:
                data = b"fixture"
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
    return asset


@unittest.skipUnless(RVM.is_file(), f"rvm binary not built: {RVM}")
class DistributionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.site = make_temp("rvm-site-")
        write_asset(cls.site, VERSION)
        (cls.site / "index.txt").write_text(f"{VERSION}\n0.0.9\n", encoding="utf-8")
        handler = partial(SimpleHTTPRequestHandler, directory=str(cls.site))
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        shutil.rmtree(cls.site, ignore_errors=True)

    def setUp(self):
        self.home = make_temp("rvm-home-")
        self.addCleanup(shutil.rmtree, self.home, True)

    def env(self, base=None):
        env = dict(os.environ)
        env["USERPROFILE"] = str(self.home)
        env["HOME"] = str(self.home)
        if base is not None:
            env["RVM_DIST_BASE"] = base
        else:
            env.pop("RVM_DIST_BASE", None)
        return env

    def rvm(self, *args, base=None, timeout=60):
        return subprocess.run([str(RVM), *args], capture_output=True, text=True,
                              env=self.env(base), timeout=timeout)

    def version_dir(self, version=VERSION) -> Path:
        return self.home / ".rvm" / "versions" / version

    def test_remote_reads_index_txt(self):
        result = self.rvm("remote", base=self.base)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(VERSION, result.stdout)
        self.assertIn("0.0.9", result.stdout)

    def test_install_and_uninstall_from_site(self):
        result = self.rvm("install", VERSION, base=self.base)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("falling back", result.stdout)
        self.assertTrue((self.version_dir() / PAYLOAD / "bin").is_dir())
        self.assertTrue((self.version_dir() / PAYLOAD / "bin" / f"rev{EXE}").is_file())
        self.assertFalse((self.version_dir() / (".download" + SUFFIX)).exists(),
                         "downloaded archive must not survive install")

        removal = self.rvm("uninstall", VERSION, base=self.base)
        self.assertEqual(removal.returncode, 0, removal.stdout + removal.stderr)
        self.assertFalse(self.version_dir().exists())

    def test_missing_version_leaves_no_directory(self):
        result = self.rvm("install", "9.9.9", base=self.base)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.version_dir("9.9.9").exists())
        # An explicit base never falls back, so the failure must be reported.
        self.assertNotIn("falling back", result.stdout)

    def test_incomplete_installation_is_repaired(self):
        self.version_dir().mkdir(parents=True)
        result = self.rvm("install", VERSION, base=self.base)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Removing incomplete installation", result.stdout)
        self.assertTrue((self.version_dir() / PAYLOAD / "bin").is_dir())

    def test_unreachable_base_fails_closed(self):
        result = self.rvm("install", VERSION, base="http://127.0.0.1:1")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.version_dir().exists())


if __name__ == "__main__":
    unittest.main()
