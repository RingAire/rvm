#!/bin/sh
# rvm installer for macOS and Linux.
#
#   curl -fsSL https://github.com/RingAire/rvm/releases/latest/download/install.sh | sh
#   sh install.sh --version 0.1.1 --prefix "$HOME/.local"
#   sh install.sh --uninstall
#
# Installs a single `rvm` binary into <prefix>/bin (default ~/.local/bin),
# verifying the release SHA256SUMS first. No sudo, no system directories.

set -eu

REPO="RingAire/rvm"
PREFIX="${RVM_PREFIX:-$HOME/.local}"
VERSION=""
UNINSTALL=0
BASE="${RVM_DOWNLOAD_BASE:-https://github.com/$REPO/releases/download}"

usage() {
    cat <<'EOF'
Usage: install.sh [options]

  --version <x.y.z>   install a specific release (default: latest)
  --prefix <dir>      install prefix; the binary goes to <dir>/bin (default: ~/.local)
  --uninstall         remove <prefix>/bin/rvm
  -h, --help          show this help

Environment:
  RVM_PREFIX          same as --prefix
  RVM_DOWNLOAD_BASE   override the download base (mirror or local test server)
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --version)
            [ $# -ge 2 ] || { echo "install.sh: --version needs a value" >&2; exit 2; }
            VERSION="$2"; shift 2 ;;
        --version=*)
            VERSION="${1#--version=}"; shift ;;
        --prefix)
            [ $# -ge 2 ] || { echo "install.sh: --prefix needs a value" >&2; exit 2; }
            PREFIX="$2"; shift 2 ;;
        --prefix=*)
            PREFIX="${1#--prefix=}"; shift ;;
        --uninstall)
            UNINSTALL=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "install.sh: unknown argument '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

BIN_DIR="$PREFIX/bin"

if [ "$UNINSTALL" -eq 1 ]; then
    if [ -f "$BIN_DIR/rvm" ]; then
        rm -f "$BIN_DIR/rvm"
        echo "rvm removed from $BIN_DIR"
    else
        echo "rvm is not installed in $BIN_DIR"
    fi
    exit 0
fi

case "$(uname -s)" in
    Darwin) OS=macos ;;
    Linux)  OS=linux ;;
    *) echo "install.sh: unsupported operating system: $(uname -s)" >&2; exit 1 ;;
esac

case "$(uname -m)" in
    arm64|aarch64) ARCH=arm64 ;;
    x86_64|amd64)  ARCH=x64 ;;
    *) echo "install.sh: unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

if [ -z "$VERSION" ]; then
    latest_url="$(curl -fsSL -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest")" || {
        echo "install.sh: cannot resolve the latest rvm release" >&2; exit 1;
    }
    VERSION="${latest_url##*/}"
    VERSION="${VERSION#v}"
fi
case "$VERSION" in
    ""|*[!0-9A-Za-z.-]*) echo "install.sh: invalid version '$VERSION'" >&2; exit 2 ;;
esac

ASSET="rvm-$VERSION-$OS-$ARCH.tar.gz"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/rvm-install.XXXXXX")"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

echo "Downloading rvm $VERSION ($OS-$ARCH)..."
curl -fsSL --retry 3 --retry-delay 1 --connect-timeout 20 \
    "$BASE/v$VERSION/$ASSET" -o "$WORK/$ASSET"
curl -fsSL --retry 3 --retry-delay 1 --connect-timeout 20 \
    "$BASE/v$VERSION/SHA256SUMS" -o "$WORK/SHA256SUMS"

EXPECTED="$(awk -v asset="$ASSET" '$2 == asset { print $1 }' "$WORK/SHA256SUMS")"
if [ -z "$EXPECTED" ]; then
    echo "install.sh: SHA256SUMS has no entry for $ASSET" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL="$(sha256sum "$WORK/$ASSET" | awk '{ print $1 }')"
else
    ACTUAL="$(shasum -a 256 "$WORK/$ASSET" | awk '{ print $1 }')"
fi
if [ "$EXPECTED" != "$ACTUAL" ]; then
    echo "install.sh: checksum mismatch for $ASSET" >&2
    exit 1
fi

tar -xzf "$WORK/$ASSET" -C "$WORK"
PAYLOAD="$WORK/rvm-$VERSION-$OS-$ARCH"
if [ ! -x "$PAYLOAD/bin/rvm" ]; then
    echo "install.sh: unexpected archive layout (bin/rvm missing)" >&2
    exit 1
fi

mkdir -p "$BIN_DIR"
cp "$PAYLOAD/bin/rvm" "$BIN_DIR/rvm"
chmod 755 "$BIN_DIR/rvm"

echo ""
echo "rvm $VERSION installed to $BIN_DIR/rvm"
case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        echo "Add it to PATH, for example:"
        echo "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.zshrc   # or ~/.bashrc"
        ;;
esac
echo "Uninstall with: sh install.sh --uninstall --prefix \"$PREFIX\""
