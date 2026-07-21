#!/usr/bin/env bash
# ws_seed_test.sh — hermetic tests for the per-worktree workspace auto-seeding
# (ws-seed.sh + build.sh's workspace resolution).
#
# Fully isolated: every scenario runs in a throwaway temp dir with its own git
# repos and a STUB bootstrap (no west, no fetch, no ~5 GB clone of the real tree).
# It never reads or writes this repo's own workspace/ or build/. cp -c (APFS
# clonefile) is exercised, but only on tiny fake trees inside the temp dir.
set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WS_SEED="$REPO/scripts/ws-seed.sh"
BUILD_SH="$REPO/scripts/build.sh"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/wsseed.XXXXXX")"
TMP="$(cd "$TMP" && pwd -P)"   # canonicalize (macOS /var -> /private/var, strip //)
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
ok()  { printf '  \033[32m✓\033[0m %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  \033[31m✗\033[0m %s\n' "$1"; fail=$((fail+1)); }
# assert NAME COND-CMD... — runs the command; pass iff it exits 0. No eval.
assert() { local n="$1"; shift; if "$@"; then ok "$n"; else bad "$n"; fi; }
# has PATTERN — greps the last captured $out (case-insensitive).
has() { printf '%s' "${out:-}" | grep -qi -- "$1"; }

# A stub bootstrap the fake worktrees carry instead of the real one: it fakes a
# bootstrapped workspace (.west + a marker) and can be told to fail, to exercise
# ws-seed's cleanup trap. No west, no network.
write_stub_bootstrap() {
	mkdir -p "$1/scripts"
	cat > "$1/scripts/bootstrap.sh" <<'EOF'
#!/usr/bin/env bash
set -e
WS="${ALIRO_WS:-$PWD/workspace}"
[ -n "${BOOTSTRAP_FAIL:-}" ] && { echo "STUB bootstrap: forced fail" >&2; exit 1; }
mkdir -p "$WS/.west" "$WS/ncs-door-lock-and-access-control"
touch "$WS/.bootstrapped"
echo "normalized" > "$WS/ncs-door-lock-and-access-control/marker.txt"
EOF
	chmod +x "$1/scripts/bootstrap.sh"
}

# Build a fake primary checkout. $2=yes seeds a fake bootstrapped workspace in it.
make_primary() {
	local dir="$1" bootstrapped="$2"
	mkdir -p "$dir"
	git -C "$dir" init -q
	git -C "$dir" config user.email t@t; git -C "$dir" config user.name t
	mkdir -p "$dir/scripts"
	cp "$WS_SEED" "$dir/scripts/ws-seed.sh"; chmod +x "$dir/scripts/ws-seed.sh"
	cp "$BUILD_SH" "$dir/scripts/build.sh"; chmod +x "$dir/scripts/build.sh"
	write_stub_bootstrap "$dir"
	if [ "$bootstrapped" = yes ]; then
		mkdir -p "$dir/workspace/.west" "$dir/workspace/ncs-door-lock-and-access-control"
		echo "primary" > "$dir/workspace/ncs-door-lock-and-access-control/marker.txt"
	fi
	printf 'workspace/\n/build/\n' > "$dir/.gitignore"
	git -C "$dir" add -A; git -C "$dir" commit -qm init
}

# Resolve WS via build.sh's resolve-only seam; return only its final line (the
# path), so any seed/bootstrap chatter on stdout is ignored.
resolve() { ( cd "$1" && shift; env "$@" ALIRO_RESOLVE_ONLY=1 ./scripts/build.sh build 2>/dev/null | tail -1 ); }

echo "== ws-seed.sh unit scenarios =="

# T1: fresh worktree seeds a local COW clone + runs (stub) bootstrap.
make_primary "$TMP/p1" yes
git -C "$TMP/p1" worktree add -q "$TMP/wt1" >/dev/null 2>&1
out="$( cd "$TMP/wt1" && ./scripts/ws-seed.sh 2>&1 )"; rc=$?
assert "T1 seed exits 0"              test "$rc" -eq 0
assert "T1 local .west created"       test -d "$TMP/wt1/workspace/.west"
assert "T1 stub bootstrap ran"        test -f "$TMP/wt1/workspace/.bootstrapped"
assert "T1 announced the clone"       has 'COW-cloning'

# T2: idempotent — a second run is a no-op (does NOT re-bootstrap).
rm -f "$TMP/wt1/workspace/.bootstrapped"
out="$( cd "$TMP/wt1" && ./scripts/ws-seed.sh 2>&1 )"; rc=$?
assert "T2 re-run exits 0"            test "$rc" -eq 0
assert "T2 reports already seeded"    has 'already seeded'
assert "T2 did NOT re-run bootstrap"  test ! -f "$TMP/wt1/workspace/.bootstrapped"

# T3: refuses to seed onto the primary checkout itself.
make_primary "$TMP/p3" no   # no workspace, so the .west guard doesn't short-circuit
out="$( cd "$TMP/p3" && ./scripts/ws-seed.sh 2>&1 )"; rc=$?
assert "T3 self-seed refused"         test "$rc" -ne 0
assert "T3 explains it is primary"    has 'primary checkout'
assert "T3 created no workspace"      test ! -e "$TMP/p3/workspace"

# T4: errors when the primary has no bootstrapped workspace to clone.
git -C "$TMP/p3" worktree add -q "$TMP/wt4" >/dev/null 2>&1
out="$( cd "$TMP/wt4" && ./scripts/ws-seed.sh 2>&1 )"; rc=$?
assert "T4 unbootstrapped errors"     test "$rc" -ne 0
assert "T4 says not bootstrapped"     has 'not bootstrapped'

# T5: a failing bootstrap removes the partial clone (cleanup trap).
git -C "$TMP/p1" worktree add -q "$TMP/wt5" >/dev/null 2>&1
out="$( cd "$TMP/wt5" && BOOTSTRAP_FAIL=1 ./scripts/ws-seed.sh 2>&1 )"; rc=$?
assert "T5 failed seed nonzero"       test "$rc" -ne 0
assert "T5 partial clone cleaned up"  test ! -e "$TMP/wt5/workspace"

echo "== build.sh resolution scenarios =="

# T6: an explicit ALIRO_WS always wins (no seeding attempted).
git -C "$TMP/p1" worktree add -q "$TMP/wt6" >/dev/null 2>&1
res="$(resolve "$TMP/wt6" ALIRO_WS=/explicit/ws)"
assert "T6 ALIRO_WS wins"             test "$res" = "/explicit/ws"
assert "T6 no seed happened"          test ! -e "$TMP/wt6/workspace"

# T7: fresh worktree auto-seeds and resolves to the LOCAL workspace.
git -C "$TMP/p1" worktree add -q "$TMP/wt7" >/dev/null 2>&1
res="$(resolve "$TMP/wt7")"
assert "T7 resolves to local ws"      test "$res" = "$TMP/wt7/workspace"
assert "T7 local ws was seeded"       test -d "$TMP/wt7/workspace/.west"

# T8: already-seeded worktree resolves local without re-seeding.
rm -f "$TMP/wt7/workspace/.bootstrapped"
res="$(resolve "$TMP/wt7")"
assert "T8 resolves local again"      test "$res" = "$TMP/wt7/workspace"
assert "T8 did not re-seed"           test ! -f "$TMP/wt7/workspace/.bootstrapped"

# T9: when seeding fails, build.sh falls back to the shared primary workspace.
git -C "$TMP/p1" worktree add -q "$TMP/wt9" >/dev/null 2>&1
res="$(resolve "$TMP/wt9" BOOTSTRAP_FAIL=1)"
assert "T9 falls back to primary"     test "$res" = "$TMP/p1/workspace"
assert "T9 left no local workspace"   test ! -e "$TMP/wt9/workspace"

echo
if [ "$fail" -eq 0 ]; then
	printf '\033[32mall %d checks passed\033[0m\n' "$pass"; exit 0
else
	printf '\033[31m%d/%d checks failed\033[0m\n' "$fail" "$((pass+fail))"; exit 1
fi
