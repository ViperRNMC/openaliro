#!/usr/bin/env bash
# docs-publish.sh — snapshot the rendered site/ onto the local gh-pages branch.
#
# The site is a build artifact and never lives on main; what gets published is a
# snapshot branch that holds site/'s contents at its root. This script only moves
# the LOCAL gh-pages ref — pushing it (`git push origin gh-pages`) stays a human
# step on purpose. Run it through `make docs-publish`, which rebuilds the site
# first so a stale or partial tree can never be snapshotted.
#
# Guards, in order:
#   - site/index.html and site/.nojekyll must exist (the build completed);
#   - docs/ must be clean: if the rebuild just changed the committed pages, they
#     must be committed first, so every snapshot corresponds to a commit;
#   - an existing gh-pages branch is reused only when it is one of our
#     snapshots ("docs site …") — a real branch by that name is never eaten;
#   - the snapshot must actually contain index.html and .nojekyll;
#   - each snapshot chains to the previous one, so the push fast-forwards.
#
# Nothing here checks out a branch or touches the working tree: the snapshot is
# built through a throwaway index, so it is safe to run from any worktree, with
# any branch checked out, dirty or not.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BRANCH=gh-pages

if ! { [ -f site/index.html ] && [ -f site/.nojekyll ]; }; then
	echo "docs-publish: site/ is missing or incomplete — run 'make docs' first." >&2
	exit 1
fi

if [ -n "$(git status --porcelain -- docs)" ]; then
	echo "docs-publish: the docs/ tree changed when the site was rebuilt." >&2
	echo "docs-publish: commit the regenerated pages first, then re-run." >&2
	exit 1
fi

# The snapshot message names the source commit; flag it when other tracked
# files are dirty, so a published site is never silently mislabeled.
SRC="$(git rev-parse --short HEAD)"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	SRC="$SRC-dirty"
fi

PARENT=""
if git rev-parse --verify -q "$BRANCH" >/dev/null; then
	SUBJECT="$(git log -1 --format=%s "$BRANCH")"
	case "$SUBJECT" in
	"docs site "*) PARENT="$BRANCH" ;;
	*)
		echo "docs-publish: a '$BRANCH' branch exists but is not a docs snapshot" >&2
		echo "docs-publish: ('$SUBJECT') — refusing to overwrite it." >&2
		exit 1
		;;
	esac
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Stage site/'s contents into a throwaway index; the real index, HEAD and the
# working tree are never involved. Finder droppings never belong on the site.
GIT_INDEX_FILE="$TMP/index" git --work-tree=site add -A .
GIT_INDEX_FILE="$TMP/index" git rm -q --cached --ignore-unmatch '*.DS_Store'
TREE="$(GIT_INDEX_FILE="$TMP/index" git write-tree)"

for need in index.html .nojekyll; do
	if ! git cat-file -e "$TREE:$need" 2>/dev/null; then
		echo "docs-publish: snapshot is missing $need — refusing to publish it." >&2
		exit 1
	fi
done

if [ -n "$PARENT" ] && [ "$(git rev-parse "$PARENT^{tree}")" = "$TREE" ]; then
	echo "gh-pages already matches site/ — nothing to publish."
	exit 0
fi

if [ -n "$PARENT" ]; then
	COMMIT="$(git commit-tree "$TREE" -p "$PARENT" -m "docs site $SRC")"
else
	COMMIT="$(git commit-tree "$TREE" -m "docs site $SRC")"
fi

if ! git branch -f "$BRANCH" "$COMMIT"; then
	echo "docs-publish: could not move '$BRANCH' — is it checked out in another" >&2
	echo "docs-publish: worktree? Detach it there, then re-run." >&2
	exit 1
fi

echo "gh-pages -> $(git rev-parse --short "$BRANCH")  (site @ $SRC)"
echo "publish with:  git push origin gh-pages"
