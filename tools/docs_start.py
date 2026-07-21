#!/usr/bin/env python3
"""Give the rendered site a real "Get started" landing.

The hero's Get-started button used to deep-link straight into the ESP32
bring-up checklist — an fine first page for exactly one kind of reader.
This pass builds start.html instead: one landing that holds every track
(hardware, toolchain, build and test, firmware internals, protocol
research, project and CI), each a card that drills down in place to the
commands, installs and guides that track needs. The page is assembled from
an existing rendered guide page, so it always carries the current shell —
sidebar, palette, theme toggle and the other passes' injections.

Also part of wayfinding, on every page:

  * the sidebar gains a Get-started entry next to Overview,
  * the search button gets the visual weight a primary control deserves
    (accent tint, a couple of attention pings on load) and the palette a
    springier open — search is how readers actually move around, so it
    should not look like chrome.

Run from the repo root, after docs_github.py and before docs_graph.py, so
the page exists before the sitewide shims and the link pass run.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

SITE = Path("site")
TEMPLATE = SITE / "RELEASING.html"
START = SITE / "start.html"

CTA_RE = re.compile(r'(<a class="btn btn-primary" href=")[^"]+(">Get started)')
NAV_RE = re.compile(r"^const NAV=(\{.*?\});$", re.M)
NAV_ANCHOR = '<script defer src="nav.js"></script>'


def repo_url() -> str:
    try:
        url = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""
    m = re.search(r"[:/]([^/:]+)/([^/]+?)(?:\.git)?$", url)
    return f"https://github.com/{m.group(1)}/{m.group(2)}" if m else ""


def chip(cmd: str) -> str:
    # Command only — no `# comment` in or next to anything copyable. Context
    # goes in prose around the chip instead.
    return (
        f'<div class="cmdchip"><span class="t-p">$</span>'
        f'<span class="c-cmd">{cmd}</span>'
        f'<button type="button" class="js-copycmd" data-cmd="{cmd}">Copy</button></div>'
    )


def row(href: str, name: str, desc: str) -> str:
    return (
        f'<li><a href="{href}"><span class="row-name">{name}</span>'
        f'<span class="row-desc">{desc}</span></a></li>'
    )


ICONS = {
    "chip": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="7" y="7" width="10" height="10" rx="2"/><path d="M9 3v2m6-2v2M9 19v2m6-2v2M3 9h2m-2 6h2M19 9h2m-2 6h2"/></svg>',
    "dl": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="M12 3v12m0 0 4-4m-4 4-4-4"/><path d="M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/></svg>',
    "play": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"><path d="M8 5v14l11-7z"/></svg>',
    "layers": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"><path d="M12 3 2 8.5 12 14l10-5.5L12 3z"/><path d="M2 15.5 12 21l10-5.5"/></svg>',
    "radio": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><circle cx="12" cy="12" r="2"/><path d="M7.8 16.2a6 6 0 0 1 0-8.4m8.4 0a6 6 0 0 1 0 8.4M4.9 19.1a10 10 0 0 1 0-14.2m14.2 0a10 10 0 0 1 0 14.2"/></svg>',
    "branch": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="6" cy="5" r="2.2"/><circle cx="6" cy="19" r="2.2"/><circle cx="18" cy="9" r="2.2"/><path d="M6 7.2v9.6M18 11.2c0 3-3 4-6 4"/></svg>',
    "bolt": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"><path d="M13 2 4.5 13.5H11L9.5 22 19 10h-6.5z"/></svg>',
}

STYLE = """<style>
.paths{display:grid;grid-template-columns:1fr 1fr;gap:.9rem;margin:1.1rem 0 2rem}
@media(max-width:860px){.paths{grid-template-columns:1fr}}
.path{border:1px solid var(--line);border-radius:14px;background:var(--card);transition:border-color .15s,box-shadow .15s}
.path:hover{border-color:var(--tint-line)}
.path[open]{grid-column:1/-1;border-color:var(--tint-line);box-shadow:var(--shadow)}
.path summary{display:flex;align-items:center;gap:.85rem;padding:.95rem 1.05rem;cursor:pointer;list-style:none}
.path summary::-webkit-details-marker{display:none}
.p-ico{flex:none;display:grid;place-items:center;width:2.2rem;height:2.2rem;border-radius:10px;background:var(--tint);color:var(--accent-ink)}
.p-ico svg{width:1.15rem;height:1.15rem}
.p-t{min-width:0}.p-t b{display:block;font-size:.95rem}.p-t small{display:block;color:var(--muted);font-size:.78rem;margin-top:.12rem}
.p-chev{margin-left:auto;color:var(--faint);transition:transform .22s;font-size:.9rem}
.path[open] .p-chev{transform:rotate(180deg)}
.p-body{padding:.2rem 1.05rem 1rem;border-top:1px solid var(--hairline)}
.path[open] .p-body{animation:p-in .28s cubic-bezier(.2,.7,.2,1) both}
@keyframes p-in{from{opacity:0;transform:translateY(-5px)}}
.p-sub{border:1px solid var(--hairline);border-radius:10px;margin:.6rem 0;background:var(--surface)}
.p-sub summary{padding:.55rem .8rem;font-size:.85rem;font-weight:600;cursor:pointer;list-style:none;display:flex;align-items:center}
.p-sub summary::-webkit-details-marker{display:none}
.p-sub summary::after{content:"⌄";margin-left:auto;color:var(--faint);line-height:.5;transition:transform .2s}
.p-sub[open] summary::after{transform:rotate(180deg)}
.p-sub .s-body{padding:.1rem .8rem .75rem;font-size:.88rem;color:var(--muted)}
.p-sub .s-body p{margin:.45rem 0}
.p-body .cmdchip{margin:.5rem 0;max-width:none}
.p-body .rows{margin:.4rem 0}
.p-body>p{font-size:.9rem;color:var(--muted);margin:.6rem 0 .2rem}
</style>"""

HERO = (
    '<header class="hero-band"><div class="hero-in">'
    '<div class="eyebrow">Start here</div><h1>Get started</h1>'
    '<p class="lede">From a clean checkout to a phone unlocking the door. '
    "Pick a track below. Each one opens in place, with the commands and "
    "guides it needs.</p>"
    "</div></header>"
)

# Every page and anchor referenced here is validated by the link pass that
# runs after this one, so a renamed guide fails the build instead of rotting.
def main_html(gh: str) -> str:
    tracks = []
    tracks.append(("chip", "Hardware", "Boards, wiring, and the UWB radio", (
        '<ul class="rows">'
        + row("nrf5340-bringup.html", "nRF5340 bring-up",
              "The primary target: DK + shields, first flash, and what a "
              "healthy boot looks like.")
        + row("esp32-bringup.html", "ESP32-S3 wiring checklist",
              "DWM3000EVB to ESP32-S3, pin by pin — the table CI keeps in "
              "sync with <code>board_pins.h</code>.")
        + row("hardware-validation.html", "Hardware validation checklist",
              "What to prove on the bench that automated CI cannot.")
        + "</ul>"
        '<details class="p-sub"><summary>Supported targets</summary>'
        '<div class="s-body">'
        "<p>Primary: <b>nRF5340 DK</b> with a <b>DWM3000EVB</b> UWB shield "
        "on the Arduino headers — the validated, measured target.</p>"
        "<p>Ports: <b>ESP32-S3</b> with the same DWM3000EVB "
        "(<code>ports/esp32</code>), and the port scaffolding under "
        "<code>ports/nrf5340dk</code>.</p></div></details>"
    )))
    tracks.append(("dl", "Software &amp; toolchain", "Everything to install, per target", (
        '<details class="p-sub" open><summary>nRF5340 — the primary target</summary>'
        '<div class="s-body">'
        + chip("nrfutil sdk-manager toolchain install --ncs-version v3.3.0")
        + chip("make bootstrap")
        + "<p>The first command runs once per machine. "
          "<code>make bootstrap</code> pulls ~6.5 GB into "
          "<code>./workspace</code>.</p>"
        + "</div></details>"
        '<details class="p-sub"><summary>ESP32-S3 port</summary>'
        '<div class="s-body"><p>ESP-IDF with esp-matter; the roadmap and the '
        "retrospective of the port live in the porting guide.</p>"
        '<ul class="rows">'
        + row("porting-esp32.html", "openaliro on ESP32-S3",
              "Porting roadmap and retrospective.")
        + "</ul></div></details>"
        '<details class="p-sub"><summary>Docs tooling</summary>'
        '<div class="s-body">'
        + chip("brew install doxygen graphviz")
        + chip("make docs")
        + "<p>The site lands in <code>./site</code>.</p>"
        + "</div></details>"
        '<ul class="rows">'
        + row("set-up.html", "Installing",
              "The full install guide — every target, every knob.")
        + "</ul>"
        '<p>The five-step version of this track sits on the '
        '<a href="index.html#get-running">landing page</a>.</p>'
    )))
    tracks.append(("play", "Build, flash &amp; test", "The make targets that drive everything", (
        chip("make build")
        + chip("make flash-erase")
        + chip("make test")
        + chip("make coverage")
        + "<p>The image lands in <code>./build/merged.hex</code>; the first "
          "flash needs the erase, plain <code>make flash</code> after. The "
          "tests run on the host — no toolchain, no hardware.</p>"
        + '<ul class="rows">'
        + row("configuring.html", "Configuring",
              "Build options, Kconfig overlays, and the runtime consoles.")
        + row("troubleshooting.html", "Troubleshooting",
              "Common issues, grouped by where they show up.")
        + "</ul>"
    )))
    tracks.append(("layers", "Firmware internals", "How the reader is put together", (
        '<ul class="rows">'
        + row("architecture.html", "Architecture",
              "The module graph, color-keyed by subsystem, and every "
              "module's declarations.")
        + row("chipset-memory.html", "Memory usage",
              "Where the nRF5340 build's flash and RAM go.")
        + row("reference.html", "Reference",
              "The Doxygen tree, generated from the declarations themselves.")
        + "</ul>"
    )))
    tracks.append(("radio", "Protocol &amp; research", "How the unlock actually works on air", (
        '<ul class="rows">'
        + row("protocol-research.html", "Protocol research",
              "BLE + UWB proximity unlock, as observed on air.")
        + row("protocol-notes.html", "Time synchronization",
              "Wall-clock time and credential validity in the firmware.")
        + row("approach-direction.html", "Approach Direction",
              "The Home app's Left/Front/Right control, end to end.")
        + row("porting-esp32-phase3.html", "Deriving the ranging key",
              "The credential auth, phase by phase.")
        + "</ul>"
    )))
    gh_rows = ""
    if gh:
        gh_rows = (
            row(gh, "Repository", "Source, issues and pull requests on GitHub.")
            + row(f"{gh}/issues", "Issues", "Report a bug or pick something up.")
        )
    tracks.append(("branch", "Project &amp; contributing", "CI, releasing, and where the work happens", (
        '<ul class="rows">' + gh_rows
        + row("RELEASING.html", "Releasing", "How a release is cut and what gates it.")
        + row("porting.html", "Porting openaliro",
              "What moving the engine to a new chipset costs, and how to prove it.")
        + "</ul>"
        '<details class="p-sub"><summary>What CI checks on every push</summary>'
        '<div class="s-body"><p>Host tests with a coverage floor, ASan/UBSan '
        "sanitizer runs, clang-format and clang-tidy, shell and workflow "
        "lint, fuzzing, CBMC proofs, port tests, firmware image builds for "
        "both targets, and a patch-drift gate that keeps the ports "
        "honest.</p></div></details>"
    )))

    cards = "".join(
        f'<details class="path"><summary><span class="p-ico">{ICONS[ico]}</span>'
        f'<span class="p-t"><b>{title}</b><small>{sub}</small></span>'
        f'<span class="p-chev">&#8964;</span></summary>'
        f'<div class="p-body">{body}</div></details>'
        for ico, title, sub, body in tracks
    )
    return (
        f'<main class="doc">\n{STYLE}\n'
        f'<div class="section-h"><h2>Pick a track</h2><span class="rule"></span></div>\n'
        f'<div class="paths">{cards}</div>\n'
        f"</main>"
    )


# Sitewide: the sidebar Get-started entry, and the search control promoted to
# the visual weight of a primary action — tinted, briefly pinging on load,
# with a springier palette open. Injected after nav.js so the tree exists.
WAYFIND = """<style>
.search-btn{background:var(--tint);border-color:var(--tint-line);color:var(--accent-ink);font-weight:600;animation:gv-ping 1.9s ease-out .9s 2}
.search-btn:hover{border-color:var(--accent);color:var(--accent-ink);box-shadow:0 0 0 3px var(--tint)}
.search-btn kbd{color:var(--accent-ink);border-color:var(--tint-line);background:var(--surface)}
@keyframes gv-ping{0%{box-shadow:0 0 0 0 var(--tint-line)}100%{box-shadow:0 0 0 10px transparent}}
.palette{transition:opacity .16s ease,transform .22s cubic-bezier(.2,.9,.3,1.15)}
.topbar .js-search{color:var(--accent-ink)}
</style>
<script id="gv-start">
(function(){function go(){var dl=document.querySelector(".tree .doclinks");
if(!dl||dl.querySelector('a[href="start.html"]'))return;
var a=document.createElement("a");a.href="start.html";
a.className="doclink"+(location.pathname.split("/").pop()==="start.html"?" on":"");
a.innerHTML='__BOLT__';
a.appendChild(document.createTextNode("Get started"));
var links=dl.querySelectorAll(".doclink");
dl.insertBefore(a,links[1]||null)}
if(document.readyState==="loading")addEventListener("DOMContentLoaded",go);
else go()})();
</script>""".replace("__BOLT__", ICONS["bolt"])


def build_page(template: str, gh: str) -> str:
    page = template
    page = re.sub(
        r"<title>[^<]*</title>", "<title>Get started</title>", page, count=1
    )
    page = re.sub(
        r'(<meta property="og:title" content=")[^"]*(")',
        r"\1Get started\2", page, count=1,
    )
    page = re.sub(r'data-active="[^"]*"', 'data-active="start"', page, count=1)
    page = re.sub(
        r'<div class="crumb">.*?</div>',
        '<div class="crumb"><b>Get started</b></div>',
        page, count=1, flags=re.S,
    )
    # function replacements: the injected HTML must land verbatim, not be
    # parsed as a replacement template (a CSS "\\23.." would read as octal)
    page = re.sub(
        r'<header class="hero-band">.*?</header>', lambda m: HERO,
        page, count=1, flags=re.S,
    )
    body = main_html(gh)
    return re.sub(
        r'<main class="doc[^"]*">.*?</main>', lambda m: body, page,
        count=1, flags=re.S,
    )


def add_search_row(page_name: str) -> None:
    nav_path = SITE / "nav.js"
    if not nav_path.is_file():
        return
    text = nav_path.read_text()
    m = NAV_RE.search(text)
    if not m:
        return
    nav = json.loads(m.group(1))
    entry = ["page", "Get started", "", page_name]
    if entry not in nav.get("search", []):
        nav["search"].insert(1, entry)
        nav_path.write_text(
            text[: m.start()] + "const NAV=" + json.dumps(nav) + ";" + text[m.end() :]
        )


def main() -> int:
    if not TEMPLATE.is_file():
        print("    no rendered site — nothing to build the landing from")
        return 0

    gh = repo_url()
    template = TEMPLATE.read_text()
    for marker in ("hero-band", NAV_ANCHOR):
        if marker not in template:
            print(
                f"docs_start: template page lacks {marker!r} — generator "
                "layout changed?", file=sys.stderr,
            )
            return 1
    START.write_text(build_page(template, gh))
    print("    start.html built")

    index = SITE / "index.html"
    if index.is_file():
        idx, n = CTA_RE.subn(r"\1start.html\2", index.read_text())
        if n:
            index.write_text(idx)
            print("    hero Get-started button now opens start.html")

    add_search_row(START.name)

    wired = kept = 0
    for p in sorted(SITE.glob("*.html")):
        content = p.read_text()
        if 'id="gv-start"' in content:
            kept += 1
            continue
        if NAV_ANCHOR not in content:
            continue
        p.write_text(content.replace(NAV_ANCHOR, NAV_ANCHOR + WAYFIND, 1))
        wired += 1
    note = f" ({kept} already wired)" if kept else ""
    print(f"    wayfinding shim on {wired} page(s){note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
