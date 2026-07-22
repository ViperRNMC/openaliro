#!/usr/bin/env bash
#
# Build + run the full host test suite (correctness gate). Plain C, no NCS
# toolchain or hardware. Compiles our logic modules against tests/host/shim and
# runs every module suite. `make coverage` builds the same sources instrumented.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"

mkdir -p "$ROOT/build"
# SAN=1: same suite rebuilt under ASan + UBSan (`make test-san`).
san_flags=
if [ -n "${SAN:-}" ]; then
  san_flags='-g -fsanitize=address,undefined -fno-sanitize-recover=all'
fi
# -w: the shim intentionally leaves some args unused, and the in-tree modules are
# lint-gated by the real Zephyr build, not here. Errors still fail the build.
# shellcheck disable=SC2086  # san_flags is a deliberate word-split flag list
"${CC:-cc}" -std=c11 -O1 -w $san_flags "${DEFS[@]}" "${INCS[@]}" \
   "${TEST_SRCS[@]}" "${SHIM_SRCS[@]}" "${UNIT_SRCS[@]}" \
   -o "$ROOT/build/host_test"
# Quiet: suites assert, they don't need the UWB diag firehose on stdout (run
# the binary directly, without WOZ_TEST_QUIET, to get it back).
WOZ_TEST_QUIET=1 "$ROOT/build/host_test"

# Host-side tooling tests (pure-stdlib Python; no toolchain involved).
python3 "$ROOT/tests/host/test_aliro_lab.py"
