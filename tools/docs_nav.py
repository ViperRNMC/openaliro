#!/usr/bin/env python3
"""Give the rendered site one curated reading order.

The generator ranks the guide list by keyword buckets, which is a reasonable
default and a poor journey: install and configure material was scattered, and
a reader finishing one page got no pointer to the next. This pass owns the
order in one place:

  * the landing page's Guides section is rebuilt into curated buckets
    (Set up first, deep dives after) — and because the sidebar shim mirrors
    the landing page's buckets, the sidebar follows automatically,
  * every page on the journey gets a prev/next pager, so there is always a
    next page and it is always the right one,
  * each guide's hero eyebrow names its bucket instead of the generic
    "Guide".

The buckets and the journey are the same list, so they cannot drift apart.
A guide added without a place in it fails the build here, on purpose: the
author decides where it belongs, or this pass would silently undo the point
of having a curated order.

Run from the repo root, after docs_start.py (start.html must exist to lead
the journey) and before docs_graph.py (whose sidebar shim reads the landing
page's buckets as rebuilt here).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SITE = Path("site")

# One entry per page, in reading order: (bucket, page slug). start.html and
# architecture.html ride the journey but are not landing-page guide rows —
# both already have their own primary placements.
JOURNEY = [
    ("Start here", "start"),
    ("Set up", "set-up"),
    ("Set up", "configuring"),
    ("Set up", "troubleshooting"),
    ("Hardware", "esp32-bringup"),
    ("Hardware", "hardware-validation"),
    ("Deep dives", "architecture"),
    ("Deep dives", "protocol-research"),
    ("Deep dives", "protocol-notes"),
    ("Deep dives", "approach-direction"),
    ("Deep dives", "porting-esp32-phase3"),
    ("Deep dives", "chipset-memory"),
    ("Porting", "porting"),
    ("Porting", "porting-esp32"),
    ("Porting", "esp32-gotchas"),
    ("Project", "reference"),
    ("Project", "RELEASING"),
]
NOT_ROWS = {"start", "architecture"}

GUIDES_RE = re.compile(
    r'(<div class="section-h"><h2>Guides</h2><span class="rule"></span></div>\n)'
    r"(.*?)"
    r'(<div class="section-h" id="subsystems">)',
    re.S,
)
ROW_RE = re.compile(r'<li><a href="([^"]+?)\.html".*?</li>', re.S)
TITLE_RE = re.compile(r"<title>([^<]*)</title>")

PAGER_CSS = """<style id="gv-pager-css">
.gv-pager{display:flex;gap:.8rem;margin:2.6rem 0 .4rem;padding-top:1.4rem;border-top:1px solid var(--line)}
.gv-pager a{flex:1;min-width:0;display:block;padding:.75rem .95rem;border:1px solid var(--line);border-radius:12px;background:var(--card);text-decoration:none;transition:border-color .15s,box-shadow .15s}
.gv-pager a:hover{border-color:var(--tint-line);box-shadow:var(--shadow)}
.gv-pager .gv-next{text-align:right}
.gv-pager .gv-lab{display:block;font-size:.72rem;letter-spacing:.04em;text-transform:uppercase;color:var(--faint);margin-bottom:.28rem}
.gv-pager .gv-pt{display:block;font-weight:600;font-size:.92rem;color:var(--accent-ink);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.gv-pager .gv-ps{display:block;font-size:.76rem;color:var(--muted);margin-top:.18rem}
</style>"""


def fail(msg: str) -> int:
    print(f"docs_nav: {msg}", file=sys.stderr)
    return 1


def page_title(slug: str) -> str:
    m = TITLE_RE.search((SITE / f"{slug}.html").read_text())
    return m.group(1) if m else slug


def curate_index(index: Path) -> int | None:
    """Rebuild the Guides section into the journey's buckets."""
    text = index.read_text()
    m = GUIDES_RE.search(text)
    if not m:
        return fail("index.html Guides section not found — generator layout changed?")

    rows = {slug: html for html, slug in
            ((r.group(0), r.group(1)) for r in ROW_RE.finditer(m.group(2)))}
    want = [slug for _, slug in JOURNEY if slug not in NOT_ROWS]
    missing = [s for s in want if s not in rows]
    extra = sorted(set(rows) - set(want))
    if missing or extra:
        return fail(
            f"guide rows and journey disagree — missing {missing or 'none'}, "
            f"unplaced {extra or 'none'}. Add new guides to JOURNEY."
        )

    out: list[str] = []
    open_bucket = None
    for bucket, slug in JOURNEY:
        if slug in NOT_ROWS:
            continue
        if bucket != open_bucket:
            if open_bucket is not None:
                out.append("</ul>")
            out.append(f'<div class="row-cap">{bucket}</div>\n<ul class="rows">')
            open_bucket = bucket
        out.append(rows[slug])
    out.append("</ul>\n")

    index.write_text(text[: m.end(1)] + "\n".join(out) + m.group(3) + text[m.end(3):])
    return None


def add_pagers() -> int | None:
    added = kept = 0
    for i, (bucket, slug) in enumerate(JOURNEY):
        page = SITE / f"{slug}.html"
        if not page.is_file():
            return fail(f"journey page {page} does not exist")
        text = page.read_text()

        if bucket != "Start here" and f'"eyebrow">Guide<' in text:
            text = text.replace('"eyebrow">Guide<', f'"eyebrow">{bucket}<', 1)

        if 'class="gv-pager"' in text:
            page.write_text(text)
            kept += 1
            continue

        cards = []
        if i > 0:
            psec, pslug = JOURNEY[i - 1]
            cards.append(
                f'<a class="gv-prev" href="{pslug}.html"><span class="gv-lab">'
                f'&#8592; Previous</span><span class="gv-pt">{page_title(pslug)}'
                f'</span><span class="gv-ps">{psec}</span></a>'
            )
        if i + 1 < len(JOURNEY):
            nsec, nslug = JOURNEY[i + 1]
            cards.append(
                f'<a class="gv-next" href="{nslug}.html"><span class="gv-lab">'
                f'Next up &#8594;</span><span class="gv-pt">{page_title(nslug)}'
                f'</span><span class="gv-ps">{nsec}</span></a>'
            )
        pager = PAGER_CSS + '\n<nav class="gv-pager" aria-label="Reading order">' \
            + "".join(cards) + "</nav>\n"

        end = text.rfind("</main>")
        if end < 0:
            return fail(f"{page} has no </main> to anchor the pager")
        page.write_text(text[:end] + pager + text[end:])
        added += 1
    note = f" ({kept} already wired)" if kept else ""
    print(f"    pager on {added} page(s){note}")
    return None


def main() -> int:
    index = SITE / "index.html"
    if not index.is_file():
        print("    no rendered site — nothing to curate")
        return 0
    rc = curate_index(index)
    if rc is not None:
        return rc
    print("    landing-page guides rebuilt into journey buckets")
    rc = add_pagers()
    return 0 if rc is None else rc


if __name__ == "__main__":
    sys.exit(main())
