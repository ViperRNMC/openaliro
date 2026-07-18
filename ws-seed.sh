#!/usr/bin/env bash
# ws-seed.sh — give this git worktree its own NCS workspace, cheaply.
#
# Frequent branch-bouncing over a single shared workspace is a trap: the tree
# holds one patch state at a time (last bootstrap wins), so a build from the
# wrong worktree silently compiles another branch's patches. This seeds a
# per-worktree workspace at the default path ($TREE/workspace) so build.sh picks
# it up with no env var, and each worktree stays self-contained.
#
# Cheap because it uses an APFS copy-on-write clone (cp -c): the clone shares
# every block with the primary and costs ~0 extra disk until a patched file
# diverges. Cleanup is automatic — the workspace lives inside the worktree, so
# deleting the worktree deletes it (see `make ws-clean`).
set -euo pipefail

TREE="$(cd "$(dirname "$0")" && pwd)"
WS="$TREE/workspace"

if [ -d "$WS/.west" ]; then
  echo "==> $WS already seeded — nothing to do"
  exit 0
fi

# Resolve the primary checkout's workspace (same logic build.sh uses to fall back).
common="$(git -C "$TREE" rev-parse --git-common-dir 2>/dev/null || true)"
[ -n "$common" ] || { echo "ERROR: not a git repo"; exit 1; }
case "$common" in /*) ;; *) common="$TREE/$common" ;; esac
primary="$(cd "$(dirname "$common")" && pwd)"

if [ "$primary" = "$TREE" ]; then
  echo "ERROR: this IS the primary checkout — run ./bootstrap.sh here, don't seed onto self"
  exit 1
fi
[ -d "$primary/workspace/.west" ] || {
  echo "ERROR: primary workspace not bootstrapped ($primary/workspace) — run ./bootstrap.sh there first"
  exit 1
}

# If we bail before the workspace is fully seeded, remove the partial clone so a
# later run (or build.sh's auto-seed) retries cleanly instead of wedging on a
# half-copied dir with no .west. cleanup preserves the real exit code — a bare
# short-circuit as the trap's last command would otherwise mask success as 1.
created="" done=""
# Cleanup handler for the workspace-seeding script's exit trap.
# Captures the last command's exit status, and if a workspace was created but the run did not
# reach completion (created set, done unset), removes WS recursively before re-exiting with the
# captured status.
cleanup() { local rc=$?; [ -n "$created" ] && [ -z "$done" ] && rm -rf "$WS"; exit "$rc"; }
trap cleanup EXIT

echo "==> COW-cloning $primary/workspace -> $WS"
created=1                             # arm cleanup before the clone so a failed cp also tidies up
cp -c -R "$primary/workspace" "$WS"   # cp -c = APFS clonefile; fails loudly off APFS

echo "==> normalizing patches to this worktree's branch"
( cd "$TREE" && ./bootstrap.sh )      # reuses the clone, re-applies THIS branch's patches

done=1
echo "    ✓ isolated workspace ready — build.sh will use it automatically"
