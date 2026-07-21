#!/usr/bin/env bash
#
# On-target build/link guard for the additive ESP32 port. The engine logic is
# covered by tests/host; this checks the things unique to THIS port that can
# silently regress as main is merged in:
#   1. the esp32s3 build still links,
#   2. the CCC STS `--wrap` seam is still wired (flags + __wrap defs present),
#   3. the excluded diagnostic files stay out of the build,
#   4. the app still fits its partition.
#
# Needs the ESP-IDF env (`. ~/esp/esp-idf/export.sh`). Skips with a clear notice
# if idf.py is absent, so the fast host test (run.sh) never depends on it.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/.." && pwd)"
BUILD="$PROJ/build"
fail=0
note() { printf '  %-4s %s\n' "$1" "$2"; }
check() { if eval "$2"; then note ok "$1"; else note FAIL "$1"; fail=1; fi; }

if ! command -v idf.py >/dev/null 2>&1; then
	echo "SKIP verify_port.sh: idf.py not on PATH (run '. ~/esp/esp-idf/export.sh' first)."
	exit 0
fi

echo "building esp32s3 target..."
( cd "$PROJ" && idf.py build >/dev/null )

echo "1. build artifacts"
check "app binary"       "[ -f '$BUILD/woz_uwb_esp32s3.bin' ]"
check "app elf"          "[ -f '$BUILD/woz_uwb_esp32s3.elf' ]"
check "partition table"  "[ -f '$BUILD/partition_table/partition-table.bin' ]"

echo "2. CCC --wrap STS seam"
NINJA="$BUILD/build.ninja"
for w in dwt_rxenable dwt_configurestsiv dwt_configurestsmode; do
	check "link flag --wrap=$w" "grep -q -- '-Wl,--wrap=$w' '$NINJA'"
done
# __wrap_* interposers must be defined (T) in some object, else the wrap is a
# no-op. (A dropped --wrap also fails the build via undefined __real_*, but this
# names the regression directly.)
wrapdef() { find "$BUILD" -name '*.obj' -exec nm {} \; 2>/dev/null | grep -qE " T $1$"; }
check "def __wrap_dwt_rxenable"         "wrapdef __wrap_dwt_rxenable"
check "def __wrap_dwt_configurestsiv"   "wrapdef __wrap_dwt_configurestsiv"
check "def __wrap_dwt_configurestsmode" "wrapdef __wrap_dwt_configurestsmode"
# The caller must still reference the unwrapped name for interposition to bite.
UWBMIN="$(find "$BUILD" -name 'uwb_min.c.obj' | head -1)"
check "uwb_min references dwt_rxenable" "[ -n '$UWBMIN' ] && nm '$UWBMIN' | grep -qE ' U dwt_rxenable$'"

echo "3. excluded diagnostic files stay out"
for d in uwb_rxdiag uwb_selftest ccc_crypto_psa aliro_shell woz_logquiet dw3000_spi_trace; do
	check "no $d.obj" "! find '$BUILD' -name '$d*.obj' | grep -q ."
done

echo "4. app fits partition"
# check_sizes.py runs at build time and fails the build on overflow; re-assert
# the app binary is smaller than the 4 MB factory partition as a direct signal.
SZ=$(stat -f%z "$BUILD/woz_uwb_esp32s3.bin" 2>/dev/null || stat -c%s "$BUILD/woz_uwb_esp32s3.bin")
check "app < 4 MB factory ($SZ B)" "[ '$SZ' -lt 4194304 ]"

echo
if [ "$fail" -eq 0 ]; then echo "verify_port: PASS"; else echo "verify_port: FAIL"; fi
exit "$fail"
