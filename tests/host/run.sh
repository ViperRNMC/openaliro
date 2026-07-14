#!/usr/bin/env bash
#
# Build + run the full host test suite (correctness gate). Plain C, no NCS
# toolchain or hardware. Compiles our logic modules against tests/host/shim and
# runs every module suite. `make coverage` builds the same sources instrumented.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"

mkdir -p "$ROOT/build"
# -w: the shim intentionally leaves some args unused, and the in-tree modules are
# lint-gated by the real Zephyr build, not here. Errors still fail the build.
cc -std=c11 -O1 -w "${DEFS[@]}" "${INCS[@]}" \
   "${TEST_SRCS[@]}" "${SHIM_SRCS[@]}" "${UNIT_SRCS[@]}" \
   -o "$ROOT/build/host_test"
exec "$ROOT/build/host_test"
