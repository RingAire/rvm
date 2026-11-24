#!/usr/bin/env bash
set -euo pipefail

# Pinned LLVM-MinGW toolchain, used by CI and the release workflow.
# Windows runners use the .zip asset (the .tar.xz contains symlinks that Git
# Bash cannot materialize); everything else uses the tarball.
version=20260616
if [[ "${RUNNER_OS:-}" == "Windows" ]]; then
  archive="llvm-mingw-${version}-ucrt-x86_64.zip"
  checksum=b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35
else
  archive="llvm-mingw-${version}-ucrt-ubuntu-22.04-x86_64.tar.xz"
  checksum=534b92e067b22a6b4441f48ae9240a3341b17825d04d577eab0cf85c44b4deda
fi
: "${RUNNER_TEMP:?RUNNER_TEMP must be set}"
: "${GITHUB_PATH:?GITHUB_PATH must be set}"
tool_dir="$(mktemp -d "${RUNNER_TEMP}/ringecho-toolchain.XXXXXX")"
curl --fail --location --retry 3 --connect-timeout 30 --max-time 600 \
  "https://github.com/mstorsjo/llvm-mingw/releases/download/${version}/${archive}" \
  --output "${tool_dir}/${archive}"
(
  cd "${tool_dir}"
  printf '%s  %s\n' "${checksum}" "${archive}" | sha256sum --check --strict
)
if [[ "${RUNNER_OS:-}" == "Windows" ]]; then
  (cd "${tool_dir}" && python -m zipfile -e "${archive}" .)
  tool_name="${archive%.zip}"
else
  (cd "${tool_dir}" && tar -xf "${archive}")
  tool_name="${archive%.tar.xz}"
fi
printf '%s\n' "${tool_dir}/${tool_name}/bin" >> "$GITHUB_PATH"
