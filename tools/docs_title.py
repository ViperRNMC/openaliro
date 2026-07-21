#!/usr/bin/env python3
"""Title the generated pages after the repository, not after the checkout directory.

Older page-generator releases took the project name from the basename of the
directory they ran in. In a linked worktree that name is the worktree
directory's, not the repository's, which put the wrong title on every page and
in the committed docs/ tree. The current release derives the name from git
itself, so this pass is a safety net that normally rewrites nothing.

The net only looks where a title can actually sit: the <title> tag, the
sidebar brand, and h1 headings in the rendered pages; the markdown H1 lines in
the two committed docs/ pages. It must not look anywhere else. A checkout can
be named after an ordinary word of the prose — a worktree named after, say,
the very thing this site is — and a blanket replacement would then rename that
word through running text, the generator's own ownership stamp (breaking its
regeneration), and the reference tree's rendered source listings. The
reference tree is excluded entirely: doxygen takes its project name from
docs/Doxyfile, never from the checkout.

The repository name comes from the common git directory, which every worktree
shares, so it is the same value from any checkout. When the two names agree
this is a no-op, which is the case in the main checkout.

Run from the repo root, after the generators and before the link pass.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# A title slot's whole span; the checkout name is then replaced only inside.
HTML_SLOTS = (
    re.compile(rb"<title>[^<]*</title>"),
    re.compile(rb'class="brand"><b>[^<]*</b>'),
    re.compile(rb"<h1[^>]*>[^<]*</h1>"),
)
MD_SLOTS = (re.compile(rb"(?m)^# .*$"),)


def git(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args], capture_output=True, text=True, check=True
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def retitle(raw: bytes, slots, token: re.Pattern[bytes], repo: bytes) -> tuple[bytes, int]:
    """Substitute the checkout name inside title slots only; count edits."""
    edits = 0

    def fix(m: re.Match[bytes]) -> bytes:
        nonlocal edits
        fixed, count = token.subn(repo, m.group(0))
        edits += count
        return fixed

    for slot in slots:
        raw = slot.sub(fix, raw)
    return raw, edits


def main() -> int:
    checkout = Path.cwd().name
    common = git("rev-parse", "--path-format=absolute", "--git-common-dir")
    if not common:
        print("docs_title: not a git checkout — leaving titles alone", file=sys.stderr)
        return 0

    repo = Path(common).parent.name
    if repo == checkout:
        print(f"    titles already correct ({repo})")
        return 0

    # Standalone token only. A plain word boundary would also match inside
    # hyphenated names and URL paths (a `worktree-pro-docs` branch, say), so
    # require that neither side continues a word, a hyphen or a path.
    name = re.escape(checkout.encode())
    token = re.compile(rb"(?<![\w/-])" + name + rb"(?![\w/-])")

    md = [Path("docs/README.md"), Path("docs/ARCHITECTURE.md")]
    html = sorted(Path("site").glob("*.html")) if Path("site").is_dir() else []

    edits = files = 0
    for path, slots in [(p, MD_SLOTS) for p in md] + [(p, HTML_SLOTS) for p in html]:
        if not path.exists():
            continue
        raw = path.read_bytes()
        fixed, count = retitle(raw, slots, token, repo.encode())
        if count:
            path.write_bytes(fixed)
            edits += count
            files += 1

    print(f"    retitled {checkout!r} -> {repo!r}: {edits} occurrence(s) in {files} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
