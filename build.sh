#!/usr/bin/env bash
#
# build.sh {build|rebuild|flash|flash-erase|build-flash} — build the Aliro
# NFC+UWB image from the self-contained ./workspace. Run ./bootstrap.sh first.
#
# Layers our modules + ISC dw3000 onto the fetched add-on via out-of-tree
# overlays. Output → ./build (git-ignored).
#
# Incremental by default — a full from-scratch (pristine) build runs only when it
# has to: first build, changed build flags (UWB chip / self-test / config), or
# when you ask for one. A preflight first checks the workspace is bootstrapped.
#
#   ./build.sh build                  # incremental where safe (fast)
#   ./build.sh rebuild                # force a clean pristine build
#   PRISTINE=1 ./build.sh build       # same as rebuild
#   UWB_SELFTEST=1 ./build.sh build   # one-shot boot self-test, no iPhone (diagnostic)
#   PRETTY=1 ./build.sh build         # curated/clean console (reversible; default verbose)
#   UWB_CHIP=dw3720 ./build.sh build  # select the plugged-in UWB chip (default: dw3000)
set -euo pipefail

TREE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="${ALIRO_WS:-$TREE/workspace}"
NCS_VER="${NCS_VER:-v3.3.0}"
OV="$TREE/integration/overlays"
ADDON="$WS/ncs-door-lock-and-access-control"
APP="$ADDON/applications/matter-aliro-door-lock-app"
BUILD="${ALIRO_BUILD:-$TREE/build}"
BOARD="nrf5340dk/nrf5340/cpuapp"

# Launch a west command through nrfutil's Nordic SDK toolchain manager for the configured NCS version. Ensures all builds use the pinned toolchain without calling bare west.
launch() { nrfutil sdk-manager toolchain launch --ncs-version "$NCS_VER" -- "$@"; }
# Compute SHA-1 hash; tries shasum first (BSD/macOS), falls back to sha1sum (Linux). Filters output to the hash hex string only.
sha()    { if command -v shasum >/dev/null 2>&1; then shasum; else sha1sum; fi; }

# --- pretty, quiet-by-default output -----------------------------------------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  BOLD=$'\033[1m'; DIM=$'\033[2m'; RED=$'\033[31m'; GRN=$'\033[32m'
  YLW=$'\033[33m'; BLU=$'\033[34m'; RST=$'\033[0m'
else
  BOLD=; DIM=; RED=; GRN=; YLW=; BLU=; RST=
fi
# Print a section header to stdout: blue "==>" followed by bold text. Used to mark the start of major build phases (preflight, build, done).
hdr() { printf '\n%s==>%s %s%s%s\n' "$BLU" "$RST" "$BOLD" "$*" "$RST"; }
# Print a checkmark to stdout in green followed by text. Used to mark successful completion of build steps.
ok()  { printf '  %s✓%s %s\n' "$GRN" "$RST" "$*"; }
# Print a key-value pair indented: dim key (9 chars wide) and value. Used to display build configuration during the build phase.
kv()  { printf '  %s%-9s%s %s\n' "$DIM" "$1" "$RST" "$2"; }
# Print an error message to stderr and exit with status 1. First line prints the error text in red; remaining arguments are printed as indented hints (dim text with arrow prefix). Used by preflight checks and build validation to fail fast on missing prerequisites or configuration errors.
die() {
  printf '\n%s✗ %s%s\n' "$RED" "$1" "$RST" >&2; shift || true
  local h; for h in "$@"; do printf '  %s→ %s%s\n' "$DIM" "$h" "$RST" >&2; done
  exit 1
}

# Resolve UWB_CHIP -> the dw3000 decadriver's chip Kconfig choice (deps/dw3000/Kconfig).
# Same DT node + wiring for both; only which *_device.c/dwt_driver builds changes.
resolve_chip() {
  case "$(printf '%s' "${UWB_CHIP:-dw3000}" | tr '[:upper:]' '[:lower:]')" in
    dw3000|dwm3000|dw3110) CHIP_FLAG="-DCONFIG_DW3000_CHIP_DW3000=y"; CHIP_NAME="DW3000/DWM3000" ;;
    dw3720|qm33|qm33xx)    CHIP_FLAG="-DCONFIG_DW3000_CHIP_DW3720=y"; CHIP_NAME="DW3720/QM33xx" ;;
    *) die "unknown UWB_CHIP='${UWB_CHIP:-}'" "use dw3000 or dw3720" ;;
  esac
}

# Verify bootstrap.sh left everything the build needs. All cheap fs/git checks.
preflight() {
  hdr "preflight"

  command -v nrfutil >/dev/null 2>&1 \
    || die "nrfutil not found on PATH" \
           "install: https://www.nordicsemi.com/Products/Development-tools/nrf-util"
  ok "nrfutil $(nrfutil --version 2>/dev/null | head -1 | awk '{print $2}')"

  { [ -d "$WS/.west" ] && [ -d "$APP" ]; } \
    || die "workspace not bootstrapped ($WS)" "run: ./bootstrap.sh"
  ok "workspace initialized"

  # bootstrap patches these three fetched repos; a pristine repo means it did not run.
  local repo unpatched=""
  for repo in "$ADDON" "$WS/nrf" "$WS/modules/lib/matter"; do
    [ -d "$repo/.git" ] || continue
    [ -n "$(git -C "$repo" status --porcelain --untracked-files=no 2>/dev/null)" ] \
      || unpatched="$unpatched $(basename "$repo")"
  done
  [ -z "$unpatched" ] \
    || die "integration patches missing on:$unpatched" "run: ./bootstrap.sh"
  ok "integration patches applied"

  local f missing=""
  for f in woz-aliro.conf dw3000-nfc.overlay pm_static.yml sysbuild-woz.conf; do
    [ -f "$OV/$f" ] || missing="$missing $f"
  done
  [ -z "$missing" ] || die "overlay files missing:$missing"
  ok "overlays present"
}

# Build the Aliro UWB firmware image. Runs preflight checks, resolves chip config, applies optional overlays (pretty console, latency diagnostics, self-test), computes a signature from all -D flags, and runs west build (pristine if config changed, incremental otherwise). Writes build signature to a cache file to detect future flag changes. Outputs merged.hex to BUILD directory.
do_build() {
  preflight
  resolve_chip

  local selftest=""
  [ "${UWB_SELFTEST:-0}" = 1 ] && selftest="-DCONFIG_WOZ_UWB_SELFTEST=y"

  # PRETTY=1: layer woz-pretty.conf after woz-aliro.conf (curated console +
  # log-level cuts). Reversible: drop PRETTY and the flag + levels revert to the
  # verbose default. It rides EXTRA_CONF_FILE (in the build signature), so
  # toggling it forces the reconfigure the changed log levels need.
  local pretty_conf=""
  [ "${PRETTY:-0}" = 1 ] && pretty_conf=";$OV/woz-pretty.conf"

  # LAT=1: layer diag-latency.conf (Matter DBG logging) to timestamp the
  # LockState ReportData egress vs the attribute set. Off-by-default diagnostic;
  # rides EXTRA_CONF_FILE (in the signature), so toggling it forces a reconfigure.
  local lat_conf=""
  [ "${LAT:-0}" = 1 ] && lat_conf=";$OV/diag-latency.conf"

  # Every -D flag that, if changed, requires a from-scratch configure. Overlay
  # *content* edits are handled incrementally by Zephyr (configure-deps), so only
  # flag changes are captured here.
  local -a dflags=(
    -DEXTRA_CONF_FILE="$OV/woz-aliro.conf${pretty_conf}${lat_conf}"
    -Dipc_radio_EXTRA_CONF_FILE="$OV/ipc_radio.conf"
    -DEXTRA_DTC_OVERLAY_FILE="$OV/dw3000-nfc.overlay"
    -DPM_STATIC_YML_FILE="$OV/pm_static.yml"
    -DSB_EXTRA_CONF_FILE="$OV/sysbuild-woz.conf"
    -DZEPHYR_EXTRA_MODULES="$TREE/modules/woz_uwb;$TREE/modules/woz_aliro_ecp;$TREE/deps/dw3000"
    -DCONFIG_DOOR_LOCK_BLE_UWB=y -DCONFIG_WOZ_UWB=y -DCONFIG_WOZ_UWB_RESPONDER=y
    -DCONFIG_WOZ_ALIRO=y -DCONFIG_DW3000=y "$CHIP_FLAG" -DCONFIG_SPI_ASYNC=y
    -DCONFIG_SHELL=n -DCONFIG_CHIP_LIB_SHELL=n -DCONFIG_NCS_SAMPLE_MATTER_TEST_SHELL=n
  )
  [ -n "$selftest" ] && dflags+=("$selftest")

  local sig sig_file="${BUILD%/}.aliro_build_sig"
  sig="$(printf '%s\0' "$BOARD" "$APP" "$NCS_VER" "${dflags[@]}" | sha | awk '{print $1}')"

  # Decide: pristine (full, slow) vs incremental (fast).
  local pristine=0 reason=""
  if [ "${PRISTINE:-0}" = 1 ]; then
    pristine=1; reason="requested"
  elif [ ! -f "$BUILD/build.ninja" ]; then
    pristine=1; reason="no configured build yet"
  elif [ ! -f "$sig_file" ]; then
    pristine=1; reason="prior build config unknown"
  elif [ "$(cat "$sig_file" 2>/dev/null)" != "$sig" ]; then
    pristine=1; reason="build flags changed"
  fi

  hdr "build"
  kv "app"   "$(basename "$APP")"
  kv "board" "$BOARD"
  kv "chip"  "$CHIP_NAME${selftest:+   (self-test ON)}${pretty_conf:+   (pretty ON)}"
  if [ "$pristine" = 1 ]; then
    kv "mode" "${YLW}pristine${RST} ${DIM}($reason)${RST}"
  else
    kv "mode" "${GRN}incremental${RST}"
  fi

  local start=$SECONDS
  if [ "$pristine" = 1 ]; then
    ( cd "$WS" && launch west build -b "$BOARD" --sysbuild "$APP" -p always -d "$BUILD" -- "${dflags[@]}" )
  else
    ( cd "$WS" && launch west build -d "$BUILD" )
  fi
  printf '%s' "$sig" > "$sig_file"
  local secs=$(( SECONDS - start ))

  hdr "done"
  ok "$BUILD/merged.hex"
  kv "time" "$(printf '%dm%02ds' $((secs/60)) $((secs%60)))"
}

# Verify that a west build has completed in BUILD directory (build.ninja exists). Called before flash operations to fail fast if build has not run.
require_built() {
  [ -f "$BUILD/build.ninja" ] || die "no build in $BUILD" "run: $0 build"
}

case "${1:-build}" in
  build)        do_build ;;
  rebuild)      PRISTINE=1; do_build ;;
  flash)        require_built; hdr "flash";         ( cd "$WS" && launch west flash -d "$BUILD" ) ;;
  flash-erase)  require_built; hdr "flash (erase)"; ( cd "$WS" && launch west flash --erase -d "$BUILD" ) ;;
  build-flash)  do_build; hdr "flash";              ( cd "$WS" && launch west flash -d "$BUILD" ) ;;
  *) echo "usage: [UWB_CHIP=dw3000|dw3720] [PRISTINE=1] $0 {build|rebuild|flash|flash-erase|build-flash}"; exit 2 ;;
esac
