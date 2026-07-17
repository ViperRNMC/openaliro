#!/usr/bin/env bash
#
# Line coverage for our own (asxeem) host-testable code, via clang source-based
# coverage. Builds the same sources as run.sh (see sources.sh) but instrumented,
# runs the suite, and reports coverage of the units under test. Files with no
# test yet show 0% — that is the point: the table lists what still needs tests.
#
# Deliberately excluded from UNIT_SRCS, and why:
#   - ccc_crypto_psa.c            on-target AES backend; the host AES double replaces it
#   - ccc_shim_wrap.c, driver/*, facade/woz_log*.c
#                                 hardware register I/O — host coverage there just
#                                 exercises stubs; these are on-target / HIL tests
#                                 (ccc_shim_rx.c graduated INTO the suite once it grew
#                                 a real state machine — listen-gate, rearm logic —
#                                 driven via recording doubles in shim/dw_rx_stub.c)
#   - modules/woz_aliro_ecp/*     Nordic-owned + NFC hardware
#
# Artifacts under build/coverage/ (build/ is gitignored). The instrumented suite
# may report test failures; coverage is still generated (execution is what counts).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"

OUT="$ROOT/build/coverage"
BIN="$OUT/host_test_cov"
mkdir -p "$OUT"

# Apple toolchains front the LLVM tools with xcrun; Linux has them on PATH bare.
llvm_tool() { if command -v xcrun >/dev/null 2>&1; then xcrun "$@"; else "$@"; fi; }

# The instrumentation flags are clang-only: macOS cc is clang, Linux CI sets CC=clang.
# -w: coverage is not a lint gate (run.sh / the Zephyr build are). Errors still fail.
"${CC:-cc}" -std=c11 -O0 -g -w "${DEFS[@]}" \
   -fprofile-instr-generate -fcoverage-mapping "${INCS[@]}" \
   "${TEST_SRCS[@]}" "${SHIM_SRCS[@]}" "${UNIT_SRCS[@]}" -o "$BIN"

# Run for coverage; a non-zero exit (failing/ pending asserts) does not abort the
# report — the code still executed. A crash (signal) would, and should.
LLVM_PROFILE_FILE="$OUT/host.profraw" "$BIN" >"$OUT/run.log" 2>&1 || true

llvm_tool llvm-profdata merge -sparse "$OUT/host.profraw" -o "$OUT/host.profdata"

# Browsable HTML, restricted to the units under test.
llvm_tool llvm-cov show "$BIN" -instr-profile="$OUT/host.profdata" "${UNIT_SRCS[@]}" \
      -format=html -output-dir="$OUT/html" \
      -show-line-counts-or-regions -show-branches=count >/dev/null

# Machine-readable summary for the terminal table.
llvm_tool llvm-cov export "$BIN" -instr-profile="$OUT/host.profdata" \
      -summary-only "${UNIT_SRCS[@]}" >"$OUT/summary.json"

python3 "$ROOT/tests/host/coverage_report.py" "$OUT/summary.json" "$OUT/html/index.html"

# Surface a failing suite without aborting the coverage report.
if ! grep -q "RESULT: PASS" "$OUT/run.log"; then
	echo "    note: suite did not report PASS — see $OUT/run.log"
fi
