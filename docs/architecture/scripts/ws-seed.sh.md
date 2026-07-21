<!-- generated documentation — edit the source, not this file -->
# `scripts/ws-seed.sh`

ws-seed.sh — give this git worktree its own NCS workspace, cheaply.
Frequent branch-bouncing over a single shared workspace is a trap: the tree
holds one patch state at a time (last bootstrap wins), so a build from the
wrong worktree silently compiles another branch's patches. This seeds a
per-worktree workspace at the default path ($TREE/workspace) so build.sh picks
it up with no env var, and each worktree stays self-contained.
Cheap because it uses an APFS copy-on-write clone (cp -c): the clone shares
every block with the primary and costs ~0 extra disk until a patched file
diverges. Cleanup is automatic — the workspace lives inside the worktree, so
deleting the worktree deletes it (see `make ws-clean`).

## API

### `cleanup()`
`scripts/ws-seed.sh:48`

Cleanup handler for the workspace-seeding script's exit trap.
Captures the last command's exit status, and if a workspace was created but the run did not
reach completion (created set, done unset), removes WS recursively before re-exiting with the
captured status.
