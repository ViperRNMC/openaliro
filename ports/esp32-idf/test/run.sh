#!/usr/bin/env bash
#
# Test entry point for the ESP32 port. Two layers, both hardware-free:
#   - test_compat_shim: fast host unit test of the pure compat headers (always).
#   - verify_port.sh:   on-target build + --wrap seam + exclusion guard (needs
#                       the ESP-IDF env; skips cleanly without it).
#
# On-target functional tests (Unity on the DW3000 SPI/IRQ path) are deferred:
# they need the DWM3000EVB wired up. See ../BRINGUP.md.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "== host: compat shim unit test =="
BIN="$(mktemp -t woz_compat_shim.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
cc -std=c11 -O1 -Wall -Wextra \
   -I "$HERE/../components/woz_uwb/compat" \
   "$HERE/test_compat_shim.c" -o "$BIN"
"$BIN"

echo
echo "== target: port build + link-seam guard =="
bash "$HERE/verify_port.sh"
