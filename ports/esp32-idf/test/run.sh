#!/usr/bin/env bash
#
# Test entry point for the ESP32 port. Three layers, all hardware-free:
#   - test_compat_shim:  fast host unit test of the pure compat headers.
#   - test_aliro_crypto: host KAT of the Aliro key-schedule core (SHA-256/KDF),
#                        compiled from the same source as the target.
#   - verify_port.sh:    on-target build + --wrap seam + exclusion guard (needs
#                        the ESP-IDF env; skips cleanly without it).
#
# On-target functional tests (Unity on the DW3000 SPI/IRQ path) are deferred:
# they need the DWM3000EVB wired up. See ../BRINGUP.md.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "== host: compat shim unit test =="
BIN="$(mktemp -t woz_compat_shim.XXXXXX)"
trap 'rm -f "$BIN" "${CBIN:-}"' EXIT
cc -std=c11 -O1 -Wall -Wextra \
   -I "$HERE/../components/woz_uwb/compat" \
   "$HERE/test_compat_shim.c" -o "$BIN"
"$BIN"

echo
echo "== host: aliro_crypto key-schedule KAT =="
# The Aliro core is shared with the nRF build; it lives in modules/woz_aliro.
ALIRO="$HERE/../../../modules/woz_aliro"
CBIN="$(mktemp -t aliro_crypto_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_crypto.c" \
   "$ALIRO/src/aliro_hash.c" "$ALIRO/src/aliro_crypto.c" \
   "$HERE/aliro_prim_host.c" -o "$CBIN"
"$CBIN"

echo
echo "== host: aliro_apdu wire-codec KAT =="
ABIN="$(mktemp -t aliro_apdu_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_apdu.c" "$ALIRO/src/aliro_apdu.c" -o "$ABIN"
"$ABIN"
rm -f "$ABIN"

echo
echo "== host: aliro_prov identity/trust KAT =="
PBIN="$(mktemp -t aliro_prov_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_prov.c" "$ALIRO/src/aliro_prov.c" -o "$PBIN"
"$PBIN"
rm -f "$PBIN"

echo
echo "== host: bolt-state LED policy =="
MATTER_MAIN="$HERE/../../esp32-matter/main"
LBIN="$(mktemp -t lock_led.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$MATTER_MAIN" \
   "$HERE/test_lock_led.c" "$MATTER_MAIN/lock_led.c" -o "$LBIN"
"$LBIN"
rm -f "$LBIN"

echo
echo "== target: port build + link-seam guard =="
bash "$HERE/verify_port.sh"
