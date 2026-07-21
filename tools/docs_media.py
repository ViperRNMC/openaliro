#!/usr/bin/env python3
"""Add the repo's imagery to the rendered site: demo screenshots and a share card.

The page generator renders prose, navigation and reference pages; it does not
publish images. This repo has three it wants on the site:

  * assets/grid-demo-light.webp / assets/grid-demo-dark.webp — one demo grid of
    the lock in use, in a light and a dark rendering (the README keeps its own,
    separate grid in assets/grid-demo.webp). Injected into the landing
    page as a figure that follows the site's theme: the toggle's `data-theme`
    attribute wins, the OS preference is the fallback — the same precedence
    the site's own stylesheet uses.
  * assets/social-preview.png — the share card. Every top-level page gets
    Open Graph / Twitter meta pointing at it, with an absolute URL derived
    from the origin remote (link scrapers ignore relative image URLs).

Idempotent on purpose: when the page generator is not configured, the earlier
passes run over a site/ kept from a previous build, so a page may already carry
the injections. Run from the repo root, after the generators and before the
link pass.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

SITE = Path("site")

DEMO_LIGHT = Path("assets/grid-demo-light.webp")
DEMO_DARK = Path("assets/grid-demo-dark.webp")
SOCIAL = Path("assets/social-preview.png")

ALT = (
    "Eight iPhone screenshots of the Aliro lock in use: Tap to Unlock and "
    "Home Key setup, approach and lock settings in the Home app, tap-to-unlock "
    "at the reader, and lock-state notifications on the Lock Screen"
)
CAPTION = (
    "The lock in Apple Home: Home Key setup, approach-direction tuning, "
    "tap to unlock, and Lock Screen state changes, all against live hardware."
)

# The landing page's first content block; the figure goes right above it, under
# the hero band. Absent from a page the generator didn't emit this way — that is
# a layout change worth failing loudly on, not papering over.
FEATS_ANCHOR = b'<ul class="feats">'
META_ANCHOR = b'<meta name="viewport" content="width=device-width, initial-scale=1">'

FIGURE = f"""<style>
.shots{{margin:2.6rem 0 .4rem;text-align:center}}
.shots img{{max-width:100%;height:auto;border-radius:16px}}
.shots figcaption{{margin-top:.9rem;font-size:.82rem;color:var(--muted)}}
.shot-dark{{display:none}}
:root[data-theme="dark"] .shot-dark{{display:inline}}
:root[data-theme="dark"] .shot-light{{display:none}}
@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]) .shot-dark{{display:inline}}
:root:not([data-theme="light"]) .shot-light{{display:none}}}}
</style>
<figure class="shots">
<img class="shot-light" src="{DEMO_LIGHT.name}" alt="{ALT}">
<img class="shot-dark" src="{DEMO_DARK.name}" alt="{ALT}">
<figcaption>{CAPTION}</figcaption>
</figure>
""".encode()


def pages_url() -> str:
    """https://<owner>.github.io/<repo> for the origin remote, or '' if none."""
    try:
        url = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""
    m = re.search(r"[:/]([^/:]+)/([^/]+?)(?:\.git)?$", url)
    if not m:
        return ""
    return f"https://{m.group(1)}.github.io/{m.group(2)}"


def social_meta(page: bytes, base: str) -> bytes:
    """The og/twitter block for one page, titled from its own <title>."""
    m = re.search(rb"<title>([^<]*)</title>", page)
    title = m.group(1).decode() if m else "openaliro"
    lines = (
        '<meta property="og:type" content="website">',
        '<meta property="og:site_name" content="openaliro">',
        f'<meta property="og:title" content="{title}">',
        f'<meta property="og:image" content="{base}/{SOCIAL.name}">',
        '<meta name="twitter:card" content="summary_large_image">',
    )
    return "\n".join(lines).encode()


def main() -> int:
    index = SITE / "index.html"
    if not index.is_file():
        print("    no rendered site — nothing to decorate")
        return 0

    missing = [p for p in (DEMO_LIGHT, DEMO_DARK, SOCIAL) if not p.is_file()]
    if missing:
        for p in missing:
            print(f"docs_media: missing {p}", file=sys.stderr)
        return 1
    for p in (DEMO_LIGHT, DEMO_DARK, SOCIAL):
        shutil.copyfile(p, SITE / p.name)

    raw = index.read_bytes()
    if b'class="shots"' in raw:
        print("    demo figure already present")
    elif FEATS_ANCHOR not in raw:
        print(
            "docs_media: landing page has no explore-card list to anchor the "
            "demo figure on — generator layout changed?",
            file=sys.stderr,
        )
        return 1
    else:
        index.write_bytes(raw.replace(FEATS_ANCHOR, FIGURE + FEATS_ANCHOR, 1))
        print("    demo figure injected into index.html")

    base = pages_url()
    if not base:
        print("    no origin remote — social meta skipped")
        return 0
    tagged = skipped = 0
    for page in sorted(SITE.glob("*.html")):
        raw = page.read_bytes()
        if b'property="og:image"' in raw:
            skipped += 1
            continue
        if META_ANCHOR not in raw:
            continue
        page.write_bytes(
            raw.replace(META_ANCHOR, META_ANCHOR + b"\n" + social_meta(raw, base), 1)
        )
        tagged += 1
    note = f" ({skipped} already tagged)" if skipped else ""
    print(f"    social card meta on {tagged} page(s){note} -> {base}/{SOCIAL.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
