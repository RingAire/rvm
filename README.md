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
- `SHA256SUMS` verification is **not implemented yet**; treat the channel as
  integrity-unverified until the manifest work lands.
- `rvm` manages only `rev`/`rem` binaries from the published dist tree; it does
  not run project code.

## License

Apache-2.0. See `LICENSE` and `NOTICE`.
