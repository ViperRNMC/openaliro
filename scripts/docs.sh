#!/usr/bin/env bash
# docs.sh — build the documentation site into site/.
#
# Two generators write into the same output directory, in this order:
#
#   1. the subsystem tree + guides + search shell   -> site/*.html
#   2. doxygen (docs/Doxyfile)                      -> site/api/
#
# then a link pass rewrites cross-document links so the published site has no
# dead ends, and the freshness gate confirms the committed docs/ tree matches
# the source. Run it through `make docs`.
#
# Nothing here needs the NCS toolchain or hardware.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Regenerating from a checkout that is behind origin/main would commit a site
# that silently reverts newer docs. Compare against the local origin/main ref
# (no fetch — run `git fetch origin` first if it might be stale).
if git rev-parse --verify -q origin/main >/dev/null; then
	BEHIND="$(git rev-list --count HEAD..origin/main)"
	if [ "$BEHIND" -gt 0 ]; then
		echo "docs.sh: HEAD is $BEHIND commit(s) behind origin/main — refusing to regenerate." >&2
		echo "docs.sh: merge or rebase onto origin/main first (git fetch origin && git merge origin/main)." >&2
		exit 1
	fi
fi

# The subsystem tree, the guide rendering and the site shell come from a page
# generator that lives outside this repo. The hook is an executable taking one
# argument, `build` or `check`, that maps it onto whatever tool this machine has.
# Without one, `make docs` still builds the reference tree and the link pass over
# the committed docs/.
#
# Searched in order. The per-checkout path is gitignored, which also means it does
# not follow a clone or a new worktree — so the config path is what a maintainer
# should actually use: one file, found from every checkout of every repo.
for candidate in \
	"${PAGE_GEN:-}" \
	"$REPO_ROOT/tools/docs_generate.local" \
	"${XDG_CONFIG_HOME:-$HOME/.config}/openaliro/docs-generate"; do
	if [ -n "$candidate" ] && [ -x "$candidate" ]; then
		PAGE_GEN="$candidate"
		break
	fi
done
PAGE_GEN="${PAGE_GEN:-}"
SKIPPED_GEN=0

for tool in doxygen dot; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "docs.sh: '$tool' not found (brew install doxygen graphviz)" >&2
		exit 1
	}
done

# Doxygen runs first: docs/reference.md links into site/api/, and the page
# generator's dead-link gate resolves that link against the rendered site.
echo "==> reference tree (doxygen)"
doxygen docs/Doxyfile

echo "==> subsystem tree, guides and site shell"
if [ -x "$PAGE_GEN" ]; then
	"$PAGE_GEN" build
else
	echo "    not configured on this machine — this is normal and not an error."
	echo "    The docs/ tree is committed, so only maintainers regenerating it"
	echo "    need the generator. Building the reference tree over it instead."
	SKIPPED_GEN=1
fi

echo "==> titles"
python3 tools/docs_title.py

echo "==> media"
python3 tools/docs_media.py

echo "==> github"
python3 tools/docs_github.py

echo "==> get-started landing"
python3 tools/docs_start.py

echo "==> reading order"
python3 tools/docs_nav.py

echo "==> graphs"
python3 tools/docs_graph.py

echo "==> reference fill"
python3 tools/docs_api.py

echo "==> link pass"
python3 tools/docs_links.py

if [ "$SKIPPED_GEN" -eq 1 ]; then
	echo "==> freshness gate (skipped: no page generator configured)"
else
	# Runs in a linked worktree too. It used to be skipped there, because the
	# title pass rewrote the checkout's name out of the generated pages and the
	# gate regenerates internally, so it saw the repair as drift. Nothing is
	# rewritten any more, so a worktree gets the same gate as anywhere else —
	# and if it ever does trip here, that is a real difference worth seeing.
	echo "==> freshness gate"
	"$PAGE_GEN" check
fi

echo
if [ "$SKIPPED_GEN" -eq 1 ]; then
	echo "reference tree only — open it with: open site/api/index.html"
else
	echo "site/index.html is ready — open it with: open site/index.html"
fi
