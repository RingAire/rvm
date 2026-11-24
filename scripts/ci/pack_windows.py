"""Create the Windows release archive from dist/<name> without external tools.

Usage: python scripts/ci/pack_windows.py <package-name>
The archive stores paths relative to dist/, so `bin/rvm.exe` lands at
`<name>/bin/rvm.exe` after extraction.
"""

import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: pack_windows.py <package-name>", file=sys.stderr)
        return 2
    name = sys.argv[1]
    dist = Path("dist")
    root = dist / name
    if not root.is_dir():
        print(f"missing package directory: {root}", file=sys.stderr)
        return 1
    target = dist / f"{name}.zip"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(dist).as_posix())
    print(target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
