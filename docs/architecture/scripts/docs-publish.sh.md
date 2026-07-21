<!-- generated documentation — edit the source, not this file -->
# `scripts/docs-publish.sh`

docs-publish.sh — snapshot the rendered site/ onto the local gh-pages branch.
The site is a build artifact and never lives on main; what gets published is a
snapshot branch that holds site/'s contents at its root. This script only moves
the LOCAL gh-pages ref — pushing it (`git push origin gh-pages`) stays a human
step on purpose. Run it through `make docs-publish`, which rebuilds the site
first so a stale or partial tree can never be snapshotted.
Guards, in order:
- site/index.html and site/.nojekyll must exist (the build completed);
- docs/ must be clean: if the rebuild just changed the committed pages, they
must be committed first, so every snapshot corresponds to a commit;
- an existing gh-pages branch is reused only when it is one of our
snapshots ("docs site …") — a real branch by that name is never eaten;
- the snapshot must actually contain index.html and .nojekyll;
- each snapshot chains to the previous one, so the push fast-forwards.
Nothing here checks out a branch or touches the working tree: the snapshot is
built through a throwaway index, so it is safe to run from any worktree, with
any branch checked out, dirty or not.
