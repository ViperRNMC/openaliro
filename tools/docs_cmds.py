#!/usr/bin/env python3
"""Render runnable command blocks as one copy chip per command.

A guide's bash block renders as a plain <pre>: the trailing `# comment` sits
in the same monospace run as the command, and the block-level copy button
copies comments and all. For a block of commands the reader wants the
opposite: each command on its own row, the comment visibly muted, and a Copy
button that yields exactly the command — the chip treatment the landing
page's quick start already uses. The chip CSS and the .js-copycmd handler
ship on every page, so the rewrite is markup only.

Only blocks that are unambiguously command sequences are touched: every
non-blank line must start with an allowlisted command (optionally prefixed
with VAR=value assignments). Device logs, pseudocode and C fragments never
match and render as before.

Run from the repo root, after docs_nav.py and before the link pass.
"""

from __future__ import annotations

import html
import re
import sys
from pathlib import Path

SITE = Path("site")
BLOCK_RE = re.compile(r"<pre><code>(.*?)</code></pre>", re.S)
LINE_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*=\S+\s+)*(?:make|cd|nrfutil|brew|idf\.py)(?:\s|$)"
)

CSS = """<style id="gv-cmds-css">
.doc .cmdchip{margin:.45rem 0;max-width:none}
.doc .cmdchip+.cmdchip{margin-top:.35rem}
</style>"""


def chip(cmd: str) -> str:
    # No trailing `# comment`, rendered or copied: a chip is the command and
    # nothing else. Whatever the comment said belongs in the guide's prose.
    return (
        f'<div class="cmdchip"><span class="t-p">$</span>'
        f'<span class="c-cmd">{html.escape(cmd)}</span>'
        f'<button type="button" class="js-copycmd" '
        f'data-cmd="{html.escape(cmd, quote=True)}">Copy</button></div>'
    )


def rewrite(match: re.Match[str], counter: list[int]) -> str:
    lines = [html.unescape(l) for l in match.group(1).split("\n")]
    body = [l for l in lines if l.strip()]
    if not body or not all(LINE_RE.match(l) for l in body):
        return match.group(0)
    chips = []
    for line in body:
        cmd = re.split(r"\s+#\s*", line, maxsplit=1)[0]
        chips.append(chip(cmd.rstrip()))
    counter[0] += 1
    return "".join(chips)


def main() -> int:
    if not SITE.is_dir():
        print("    no rendered site — nothing to rewrite")
        return 0
    blocks, pages = 0, 0
    for page in sorted(SITE.glob("*.html")):
        text = page.read_text()
        counter = [0]
        out = BLOCK_RE.sub(lambda m: rewrite(m, counter), text)
        if not counter[0]:
            continue
        if 'id="gv-cmds-css"' not in out:
            out = out.replace("</main>", CSS + "</main>", 1)
        page.write_text(out)
        blocks += counter[0]
        pages += 1
    print(f"    {blocks} command block(s) -> copy chips on {pages} page(s)")
    if blocks == 0:
        print("docs_cmds: no block matched — allowlist stale?", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
