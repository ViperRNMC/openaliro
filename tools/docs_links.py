#!/usr/bin/env python3
"""Repair cross-document links in the rendered site, then assert none are left broken.

The guide pages are authored as markdown that must also read correctly on GitHub,
so they link to other documents as `other.md` and to sources as `../modules/x.c`.
Neither form resolves once the pages are rendered into site/:

  * `other.md`            -> `other.html`, when that page was rendered
  * `../modules/x.c`      -> the file on GitHub, since sources are not published

Anything still unresolved after the rewrite is a genuine broken link and fails
the build. Run from the repo root, after both generators.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

SITE = Path("site")
HREF = re.compile(rb'href="([^"]+)"')
# Not addresses of anything on disk: external, in-page, or generator-injected.
SKIP_PREFIXES = ("http://", "https://", "#", "mailto:", "data:", "javascript:")


def blob_base() -> str:
    """github.com/<owner>/<repo>/blob/<branch> for the current remote, or '' if none."""
    def git(*args: str) -> str:
        try:
            return subprocess.run(
                ["git", *args], capture_output=True, text=True, check=True
            ).stdout.strip()
        except subprocess.CalledProcessError:
            return ""

    url = git("remote", "get-url", "origin")
    if not url:
        return ""
    # git@host:owner/repo.git  |  https://host/owner/repo.git
    m = re.search(r"[:/]([^/:]+)/([^/]+?)(?:\.git)?$", url)
    if not m:
        return ""
    # The remote's default branch, never the working branch: a feature or
    # worktree branch is usually unpushed, so links into it would 404.
    head = git("symbolic-ref", "--short", "refs/remotes/origin/HEAD")
    branch = head.split("/", 1)[1] if "/" in head else "main"
    return f"https://github.com/{m.group(1)}/{m.group(2)}/blob/{branch}"


_LISTING: dict[Path, dict[str, str]] = {}


def _entries(directory: Path) -> dict[str, str]:
    """{lowercased name: real name} for a directory, cached. macOS is
    case-insensitive, so `Path.exists()` happily confirms `ARCHITECTURE.html`
    when the file on disk is `architecture.html`; a case-sensitive web host
    then serves a 404. Every existence check here goes through this map so the
    case a link claims is the case that actually exists."""
    if directory not in _LISTING:
        try:
            _LISTING[directory] = {e.name.lower(): e.name for e in directory.iterdir()}
        except OSError:
            _LISTING[directory] = {}
    return _LISTING[directory]


def _real(path: Path) -> Path | None:
    """`path` with the casing it actually has on disk, or None when absent."""
    real = _entries(path.parent).get(path.name.lower())
    return path.parent / real if real else None


def _exact(path: Path) -> bool:
    """True only when `path` exists with exactly this casing."""
    return _entries(path.parent).get(path.name.lower()) == path.name


def main() -> int:
    if not SITE.is_dir():
        print("docs_links: site/ not found — run the generators first", file=sys.stderr)
        return 1

    base = blob_base()
    site_abs = SITE.resolve()
    pages = sorted(SITE.rglob("*.html"))
    rewritten = 0
    broken: list[tuple[str, str]] = []

    for page in pages:
        raw = page.read_bytes()

        def fix(match: re.Match[bytes]) -> bytes:
            nonlocal rewritten
            href = match.group(1).decode()
            if href.startswith(SKIP_PREFIXES):
                return match.group(0)

            target, _, frag = href.partition("#")
            if not target:
                return match.group(0)

            # A link to a document that was rendered as its own page.
            if target.endswith(".md"):
                candidate = _real((page.parent / target).with_suffix(".html"))
                if candidate is not None:
                    new = os.path.relpath(candidate, page.parent)
                    rewritten += 1
                    return b'href="' + (new + (f"#{frag}" if frag else "")).encode() + b'"'

            # Doxygen emits index links for the "_" bucket (globals__.html,
            # globals_func__.html) even when no symbol starts with an underscore,
            # and then never writes those files. Point them at the real index.
            if re.fullmatch(r"globals(_\w+)?__\.html", target):
                real = target.replace("__.html", ".html")
                if (page.parent / real).exists():
                    rewritten += 1
                    return b'href="' + real.encode() + b'"'

            # A link that escapes the site: it addresses the repo, not the site.
            resolved = (page.parent / target).resolve()
            if not str(resolved).startswith(str(site_abs)):
                repo_rel = os.path.relpath(resolved, Path.cwd())
                if base and _real(resolved) is not None:
                    rewritten += 1
                    return b'href="' + f"{base}/{repo_rel}".encode() + b'"'

            return match.group(0)

        fixed = HREF.sub(fix, raw)
        if fixed != raw:
            page.write_bytes(fixed)

    # The Reference tree is a build artifact, so an authored page cannot link to
    # it: on GitHub the target does not exist, and the generator's link gate
    # rightly rejects it. docs/reference.md therefore names the path in prose;
    # here, where the tree does exist, that mention becomes the real link.
    api = SITE / "api" / "index.html"
    if api.is_file():
        for page in pages:
            if str(api) in str(page):
                continue
            raw = page.read_bytes()
            span = b"<code>site/api/index.html</code>"
            if span not in raw:
                continue
            rel = os.path.relpath(api, page.parent)
            link = b'<a href="' + rel.encode() + b'"><code>site/api/index.html</code></a>'
            out, pos = bytearray(), 0
            while (i := raw.find(span, pos)) != -1:
                out += raw[pos:i]
                head = raw[:i]
                # a mention already inside an anchor stays plain text: nesting an
                # <a> in an <a> is invalid and splits the outer link in the browser
                if head.rfind(b"<a ") > head.rfind(b"</a>"):
                    out += span
                else:
                    out += link
                    rewritten += 1
                pos = i + len(span)
            out += raw[pos:]
            page.write_bytes(bytes(out))

    # Verify: every remaining relative link must resolve inside site/.
    for page in pages:
        for m in HREF.finditer(page.read_bytes()):
            href = m.group(1).decode()
            if href.startswith(SKIP_PREFIXES):
                continue
            target = href.partition("#")[0]
            if not target:
                continue
            if not _exact((page.parent / target).resolve()):
                broken.append((page.as_posix(), href))

    print(f"    {rewritten} link(s) rewritten across {len(pages)} page(s)")
    if broken:
        print(f"docs_links: {len(broken)} broken link(s):", file=sys.stderr)
        for src, href in broken[:20]:
            print(f"    {src} -> {href}", file=sys.stderr)
        return 1
    print("    no broken links")
    return 0


if __name__ == "__main__":
    sys.exit(main())
