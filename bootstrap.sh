#!/usr/bin/env bash
#
# bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream.
#
# Fetches everything the build needs from public GitHub into ./workspace
# (git-ignored), then applies our integration patches on top. It never reads from
# any other local checkout — a clean upstream fetch every time.
#
# Fetches (all public):
#   - Nordic add-on  ncs-door-lock-and-access-control @ the pin below
#   - NCS v3.3.0 + Zephyr + every module (via the add-on's own west manifest)
#
# Prereq (once per machine): nRF Connect SDK v3.3.0 toolchain
#   nrfutil sdk-manager toolchain install --ncs-version v3.3.0
#
# Usage:  ./bootstrap.sh                       # workspace in ./workspace
#         ALIRO_WS=/big/disk/ws ./bootstrap.sh # put the multi-GB workspace elsewhere
set -euo pipefail

TREE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="${ALIRO_WS:-$TREE/workspace}"
NCS_VER="${NCS_VER:-v3.3.0}"
PIN="a5ad7fde1041d81690710a949c98eda1985fee0b"     # ncs-door-lock-and-access-control (public)
ADDON_URL="https://github.com/nrfconnect/ncs-door-lock-and-access-control"
ADDON="$WS/ncs-door-lock-and-access-control"
P="$TREE/integration/patches"

# Launch the nRF Util SDK manager toolchain with the configured NCS version, passing through all remaining arguments.
launch() { nrfutil sdk-manager toolchain launch --ncs-version "$NCS_VER" -- "$@"; }

# 1. Fetch pristine upstream into $WS.
echo "==> workspace: $WS   (add-on pin ${PIN:0:10}…, NCS $NCS_VER)"
if [ ! -d "$ADDON/.git" ]; then
  # Clone the manifest repo, checkout the pinned SHA, then `west init -l`.
  # (`west init -m … --mr <SHA>` is wrong: it runs `git clone --branch <SHA>`, and
  #  --branch only accepts a tag or branch name, never a commit SHA.)
  mkdir -p "$WS"
  git clone -q "$ADDON_URL" "$ADDON"
  git -C "$ADDON" checkout -q "$PIN"
  launch west init -l "$ADDON"
  echo "    west update — fetching NCS + modules from GitHub (multi-GB, first run)"
  ( cd "$WS" && launch west update )
else
  echo "    already initialized — reusing (delete $WS for a clean re-fetch)"
fi

# 2. Apply our patches on top. Each target repo is reset to its pinned HEAD and
#    verified clean first, so a patch can never land on unexpected local state.
echo "==> applying integration patches"
# Apply patch files to a repository, ensuring it is pristine (no uncommitted changes) before patching.
apply_to() {   # $1 = repo, remaining args = patch files
  local repo="$1"; shift
  git -C "$repo" checkout -q -- .
  [ -z "$(git -C "$repo" status --porcelain --untracked-files=no)" ] \
    || { echo "ERROR: $repo not pristine — refusing to patch"; exit 1; }
  git -C "$repo" apply --whitespace=nowarn "$@"
}
apply_to "$ADDON"                 "$P/custom_impl-uwb.patch" "$P/crypto-timesync-tap.patch" "$P/pretty-shell.patch" "$P/console-quiet-flood.patch" "$P/kpersistent-orphan-selfheal.patch" "$P/aliro-doc-time-ratchet.patch" "$P/aliro-time-persist.patch" "$P/extnvs-rollback-mirror-id.patch"
apply_to "$WS/nrf"                "$P/nrf-flashfit-dfu-guards.patch"
apply_to "$WS/modules/lib/matter" "$P/matter-ble-multi-identity.patch"
echo "    ✓ pristine upstream + 10 patches (add-on ×8, nrf, matter)"

echo "==> ready. Build with:  $TREE/build.sh build"
