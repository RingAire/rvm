# rvm — RingEcho Version Manager

`rvm` is the version manager for the [RingEcho](https://github.com/RingAire/ringecho-lang-c)
compiler toolchain, in the spirit of `nvm`. It installs and switches between
released compiler versions without touching the compiler source tree.

This project is a **separate repository** from the compiler. It consumes the
published distribution contract; it never builds the compiler itself.

## Commands

```text
rvm install <version>   install a released RingEcho version
rvm use <version>       switch to an installed version
rvm list                list installed versions
rvm current             show the selected version
rvm uninstall <version> remove a version (clears the selection if it was active)
rvm remote              list published versions
rvm version             print the rvm version
```

## Install

Windows (PowerShell, per-user, no administrator rights):

```powershell
irm https://github.com/RingAire/rvm/releases/latest/download/install.ps1 -OutFile install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

The installer verifies `SHA256SUMS`, installs to
`%LOCALAPPDATA%\Programs\rvm`, adds that directory to the user PATH, and
registers an entry in Settings > Apps. Uninstall from there, or run
`%LOCALAPPDATA%\Programs\rvm\uninstall.ps1`.

macOS / Linux:

```sh
curl -fsSL https://github.com/RingAire/rvm/releases/latest/download/install.sh | sh
```

Installs to `~/.local/bin`; the script verifies `SHA256SUMS`. For options
(custom prefix, specific version) download it first:

```sh
curl -fsSL https://github.com/RingAire/rvm/releases/latest/download/install.sh -o install.sh
sh install.sh --prefix /usr/local
sh install.sh --uninstall
```

## Distribution contract

`rvm` resolves versions under one base URL:

```
<base>/index.txt                                  one version per line
<base>/v<version>/ringecho-<version>-<platform>-<arch>.zip      Windows
<base>/v<version>/ringecho-<version>-<platform>-<arch>.tar.gz   Linux / macOS
```

- `<base>` defaults to `https://ringecho-lang.org/dist`.
- If the site is unreachable or the asset is missing, `install` falls back to
  the identically named GitHub release asset and prints a notice. An explicit
  `RVM_DIST_BASE` override never falls back.
- `RVM_DIST_BASE` also selects a mirror or a local test server:
  `RVM_DIST_BASE=http://127.0.0.1:8080 rvm install 0.3.0`.
- Installed layout: `~/.rvm/versions/<version>/<payload>/bin`, selected via
  `~/.rvm/current`.

## Build

```sh
make                 # target/Release/rvm
make CONFIG=Debug
make test            # distribution-contract tests (local HTTP server)
```

Requires a C11 compiler, `curl`, and `tar` (`bsdtar` on Windows).

## Security notes

- Downloads go through `curl -fsL`; a failed install rolls back its version
  directory, and an incomplete directory is replaced on retry.
- The install scripts verify `SHA256SUMS`. `rvm install` (compiler versions)
  does not verify checksums yet; treat that path as integrity-unverified until
  the manifest work lands.
- `rvm` manages only `rev`/`rem` binaries from the published dist tree; it does
  not run project code.

## License

Apache-2.0. See `LICENSE` and `NOTICE`.
