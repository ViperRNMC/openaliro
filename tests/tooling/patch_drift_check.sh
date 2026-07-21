#!/usr/bin/env bash
#
# patch_drift_check.sh — verify the integration patches still apply to the pinned
# upstream revisions, without the multi-GB workspace. Mirrors bootstrap.sh: it
# resolves the add-on pin, then the nrf and matter revisions through the chained
# west manifests, sparse-fetches just the files each repo's patches touch, and
# applies those patches for real (same grouping and order as bootstrap.sh) in a
# throwaway clone.
#
# Read-only network fetches from public GitHub; nothing in this repo is touched.
# Usage: tests/tooling/patch_drift_check.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
P="$ROOT/ports/nrf5340dk/patches"

# The add-on pin lives in bootstrap.sh (the applier); west.yml documents the same
# pin. The two drifting apart is itself a failure.
PIN="$(sed -n 's/^PIN="\([0-9a-f]\{40\}\)".*/\1/p' "$ROOT/scripts/bootstrap.sh")"
[ -n "$PIN" ] || { echo "ERROR: could not read PIN from bootstrap.sh" >&2; exit 1; }
grep -q "revision: $PIN" "$ROOT/west.yml" \
  || { echo "ERROR: west.yml revision != bootstrap.sh PIN ($PIN)" >&2; exit 1; }

# Per-repo patch lists, in bootstrap.sh's apply order. The HA patches are checked
# even though the default build skips them (HA=1 opts in): drift is about whether
# a patch still applies upstream, not about whether this build uses it. Order
# matters — ha-occupancy-endpoint is cut against a tree with the others applied.
ADDON_PATCHES=(
  "$P/custom_impl-uwb.patch" "$P/crypto-timesync-tap.patch"
  "$P/pretty-shell.patch" "$P/console-quiet-flood.patch"
  "$P/kpersistent-orphan-selfheal.patch" "$P/aliro-doc-time-ratchet.patch"
  "$P/aliro-time-persist.patch" "$P/extnvs-rollback-mirror-id.patch"
  "$P/approach-direction-cluster.patch"
  "$P/ha-lockoperation-event.patch" "$P/ha-occupancy-endpoint.patch"
)
NRF_PATCHES=("$P/nrf-flashfit-dfu-guards.patch")
MATTER_PATCHES=("$P/matter-ble-multi-identity.patch")

# A patch no list names is never checked, so it rots silently at the next pin
# bump. Refuse to pass rather than under-report coverage.
covered="$(printf '%s\n' "${ADDON_PATCHES[@]}" "${NRF_PATCHES[@]}" "${MATTER_PATCHES[@]}" \
  | sed 's|.*/||' | sort)"
ondisk="$(cd "$P" && printf '%s\n' *.patch | sort)"
[ "$covered" = "$ondisk" ] || {
  echo "ERROR: ports/nrf5340dk/patches/ and this script's lists disagree" >&2
  echo "       (< = listed but absent, > = present but unchecked)" >&2
  diff <(printf '%s\n' "$covered") <(printf '%s\n' "$ondisk") >&2 || true
  exit 1
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# west_field <west.yml> <project-name> <key> — value of <key> in that project block.
west_field() {
  awk -v proj="$2" -v key="$3" '
    /^[[:space:]]*-[[:space:]]*name:/ { cur = $NF }
    cur == proj && $1 == key ":" { print $NF; exit }' "$1"
}

# fetch_sparse <url> <rev> <dir> <patch...> — blobless clone holding only the files
# the patches touch, checked out at <rev>.
fetch_sparse() {
  local url="$1" rev="$2" dir="$3"; shift 3
  local paths
  paths="$(git apply --numstat "$@" | cut -f3)"
  case "$paths" in *'=>'*) echo "ERROR: rename patches unsupported" >&2; exit 1;; esac
  git init -q "$dir"
  git -C "$dir" remote add origin "$url"
  # shellcheck disable=SC2086  # one sparse pattern per patched path
  git -C "$dir" sparse-checkout set --no-cone $paths
  git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$rev"
  git -C "$dir" checkout -q FETCH_HEAD
}

# check <label> <url> <rev> <patch...> — fetch, then apply exactly as bootstrap.sh
# does: one git-apply invocation, same patch order.
check() {
  local label="$1" url="$2" rev="$3"; shift 3
  printf '  %-7s %s @ %.10s…' "$label" "${url##*github.com/}" "$rev"
  fetch_sparse "$url" "$rev" "$WORK/$label" "$@"
  git -C "$WORK/$label" apply --whitespace=nowarn "$@"
  printf '  ✓ %d patch(es)\n' "$#"
}

echo "==> patch drift check (add-on pin ${PIN:0:10}…)"
check addon "https://github.com/nrfconnect/ncs-door-lock-and-access-control" "$PIN" \
  "${ADDON_PATCHES[@]}"

# nrf revision from the add-on's manifest at the pin (blob fetched on demand).
git -C "$WORK/addon" show FETCH_HEAD:west.yml >"$WORK/addon-west.yml"
NRF_REV="$(west_field "$WORK/addon-west.yml" nrf revision)"
NRF_REPO="$(west_field "$WORK/addon-west.yml" nrf repo-path)"
[ -n "$NRF_REV" ] || { echo "ERROR: no nrf revision in add-on west.yml" >&2; exit 1; }
check nrf "https://github.com/nrfconnect/${NRF_REPO:-sdk-nrf}" "$NRF_REV" \
  "${NRF_PATCHES[@]}"

# matter revision from sdk-nrf's manifest at that revision.
git -C "$WORK/nrf" show FETCH_HEAD:west.yml >"$WORK/nrf-west.yml"
MATTER_REV="$(west_field "$WORK/nrf-west.yml" matter revision)"
MATTER_REPO="$(west_field "$WORK/nrf-west.yml" matter repo-path)"
[ -n "$MATTER_REV" ] || { echo "ERROR: no matter revision in sdk-nrf west.yml" >&2; exit 1; }
check matter "https://github.com/nrfconnect/${MATTER_REPO:-sdk-connectedhomeip}" "$MATTER_REV" \
  "${MATTER_PATCHES[@]}"

echo "    ✓ all $(( ${#ADDON_PATCHES[@]} + ${#NRF_PATCHES[@]} + ${#MATTER_PATCHES[@]} )) patches apply cleanly at the pinned revisions"
