#!/usr/bin/env python3
"""Publish the browser flasher: site/flash/ = the web-flasher/ page + firmware.

The page and its ESP Web Tools manifest are committed in web-flasher/; the
merged firmware image is not. GitHub release assets are served without CORS
headers (probed 2026-07-22: neither the github.com redirect nor its CDN
answers Access-Control-Allow-Origin), so the browser cannot fetch the image
from the release; it has to sit next to the page on the same origin. This
pass stages it at site-build time, preferring in order:

  1. web-flasher/openaliro-matter-lock.bin (gitignored): a local
     `idf.py merge-bin` output for bench runs, published with the committed
     manifest (version "dev").
  2. The latest release's loose assets (openaliro-matter-lock.bin +
     openaliro-matter-lock.manifest.json, uploaded by release.yml), fetched
     server side where CORS does not apply; the manifest arrives already
     version-stamped.
  3. Neither: skip the page entirely, loudly. An Install button whose
     firmware 404s is worse than no page, and before the first release this
     is the normal state of a fresh checkout.

When the page is staged, the site links to it: a row in the get-started hub's
Hardware bucket and a one-line lead under the landing page's "Get running"
heading. Injected here and not in the sources on purpose — the flash page
only exists when firmware was found, and a committed link would 404 on every
checkout without a release. No firmware, no links, nothing dangles.

Run from the repo root, after the link pass: the page is standalone and its
links are absolute or flash-local, so it needs no rewriting. docs.sh drives it.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

SITE = Path("site")
SRC = Path("web-flasher")
FIRMWARE = "openaliro-matter-lock.bin"
RELEASE_MANIFEST = "openaliro-matter-lock.manifest.json"

# Site links to the staged page, each anchored on markup the generator emits
# today; if an anchor drifts, the injection is skipped with a warning rather
# than guessed. Markers keep every edit idempotent over a kept site/.
MARK = "<!-- flash-page -->"

HUB_ANCHOR = '<li><a href="hardware-validation.html">'
HUB_ROW = (
    MARK + '<li><a href="flash/"><span class="row-name">Flash ESP32-S3 from '
    "the browser</span><span class=\"row-desc\">No toolchain: this site writes "
    "the merged firmware image over WebSerial (Chrome, Edge, or "
    "Firefox).</span></a></li>"
)

QS_ANCHOR = '<div class="section-h"><h2>Get running</h2><span class="rule"></span></div>'
# Same card the digital twin uses at the foot of this section: the twin pass
# leaves its .twin-cta styles on the landing page, so reusing the class keeps
# the two cards identical by construction. Only the margins differ — the twin
# card closes the section, this one opens it, ahead of the toolchain steps.
QS_LEDE = (
    MARK + '<a class="twin-cta qs-flash" href="flash/">'
    '<span class="tc-ic">&#x26A1;</span>'
    '<span class="tc-t"><b>No toolchain handy?</b><span>Flash the ESP32-S3 '
    "build straight from the browser &mdash; this site writes the firmware "
    "over WebSerial (Chrome, Edge, or Firefox), then pick up at "
    "commissioning.</span></span>"
    '<span class="tc-go">Open the flasher &rarr;</span></a>'
)

QS_CSS = (
    "\n/* flash-page */\n"
    ".qs-flash{margin:1.4rem 0 2.4rem}\n"
)


def repo_slug() -> str:
    """owner/repo for the origin remote, or '' if none."""
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
    return f"{m.group(1)}/{m.group(2)}"


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=60) as resp:
        return resp.read()


def inject(page: Path, anchor: str, addition: str, before: bool) -> str:
    """Insert addition next to anchor in page, once; report what happened."""
    if not page.is_file():
        return "missing"
    html = page.read_text()
    if MARK in html:
        return "already linked"
    if anchor not in html:
        return "anchor not found — skipped"
    new = addition + anchor if before else anchor + addition
    page.write_text(html.replace(anchor, new, 1))
    return "linked"


def link_site() -> None:
    hub = inject(SITE / "start.html", HUB_ANCHOR, HUB_ROW, before=True)
    qs = inject(SITE / "index.html", QS_ANCHOR, QS_LEDE, before=False)
    sheet = SITE / "style.css"
    if qs == "linked" and sheet.is_file() and "/* flash-page */" not in sheet.read_text():
        with sheet.open("a") as f:
            f.write(QS_CSS)
    if qs == "linked" and ".twin-cta{" not in (SITE / "index.html").read_text():
        print("    note: no .twin-cta styles on the landing page — flash card unstyled")
    print(f"    site links: get-started hub {hub}, landing quickstart {qs}")


def main() -> int:
    if not SITE.is_dir():
        print("docs_flash: site/ not found — run the generators first", file=sys.stderr)
        return 1

    dst = SITE / "flash"
    shutil.rmtree(dst, ignore_errors=True)

    local = SRC / FIRMWARE
    if local.is_file():
        dst.mkdir(parents=True)
        shutil.copyfile(local, dst / FIRMWARE)
        shutil.copyfile(SRC / "manifest.json", dst / "manifest.json")
        source = f"local {local} (manifest version dev)"
    else:
        slug = repo_slug()
        if not slug:
            print("    no origin remote and no local image — flash page skipped")
            return 0
        base = f"https://github.com/{slug}/releases/latest/download/"
        try:
            image = fetch(base + FIRMWARE)
            manifest = fetch(base + RELEASE_MANIFEST)
        except OSError as err:
            print(f"    no local image and no release asset ({err})")
            print("    flash page skipped — publish a release or drop a merged")
            print(f"    image at {local} and rerun")
            return 0
        dst.mkdir(parents=True)
        (dst / FIRMWARE).write_bytes(image)
        (dst / "manifest.json").write_bytes(manifest)
        source = f"latest release ({len(image)} bytes)"

    shutil.copyfile(SRC / "index.html", dst / "index.html")
    print(f"    {dst / 'index.html'} (firmware: {source})")
    link_site()
    return 0


if __name__ == "__main__":
    sys.exit(main())
