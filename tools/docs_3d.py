#!/usr/bin/env python3
"""Render the whole code surface as a flyable 3D graph: site/graph3d.html.

The architecture page's 2D graphs show the curated module clusters. This pass
builds the immersive counterpart over the entire tree — every source file the
docs cover, from the reader engine to the flash scripts — as a 3D force graph
you can orbit, filter and fly through. Clicking a file opens a panel with its
description, its imports both ways, and its API symbols, each linking into the
reference tree.

Everything is mined from what the repo already publishes, no extra analysis:

  * docs/architecture/<group>/<file>.md — one node per page: the H1 carries
    the source path, the first paragraph the description, the "**depends on**"
    row the outgoing edges. Reversing those edges gives "used by".
  * site/nav.js — the search index built earlier in the pipeline; its
    function/class/macro entries, keyed by page slug, become each node's
    symbol list with working anchors.
  * architecture.html's gv-slots marker — the cluster -> color slot map the
    2D graphs persisted, so both views and the sidebar dots stay color-keyed
    alike; directories beyond the curated clusters get slots from an extended
    palette.

The renderer is the 3d-force-graph bundle (MIT), vendored under
internal/vendor/ (gitignored) and copied into site/; when the vendor copy is
missing it is fetched once from unpkg. Offline with no vendor copy, the pass
skips cleanly: no page, no entry button, the 2D graphs stand alone.

The stage is always dark — same rule as the site's code panels — while the
page chrome follows the reader's theme (dm-theme, then the OS preference).

Run from the repo root, after the reference fill (the symbol index must be
complete) and before the link pass (which validates the links minted here).
"""

from __future__ import annotations

import json
import re
import shutil
import sys
import urllib.request
from pathlib import Path

SITE = Path("site")
ARCH = Path("docs/architecture")
PAGE = SITE / "graph3d.html"
LIB = "3d-force-graph.min.js"
VENDOR = Path("internal/vendor") / LIB
LIB_URL = "https://unpkg.com/3d-force-graph@1/dist/3d-force-graph.min.js"

CTA_MARK = 'id="gv-3d"'
CTA_ANCHOR = '<script type="application/json" id="gv-slots">'

H1 = re.compile(r"^# \[?`([^`]+)`", re.M)
DEPS = re.compile(r"\*\*depends on\*\* (.+)")
LINK = re.compile(r"\[`[^`]*`\]\(([^)]+\.md)\)")
MD_LINK = re.compile(r"\[`?([^`\]]*)`?\]\([^)]*\)")

# Slots 0-4 mirror the 2D graphs (gv-slots); the rest extend the family for
# directories the curated clusters never covered. Read against the dark stage.
COLORS = [
    "#3987e5", "#d95926", "#199e70", "#c98500", "#d55181",
    "#8b7cf0", "#18b1c4", "#b5b53a", "#e0524f", "#7f8ea3",
]


def cluster(path: str) -> str:
    """Directory key for color grouping — same shape the 2D sidebar dots use."""
    d = path.rsplit("/", 1)[0] if "/" in path else path
    if d.startswith("modules/"):
        d = d[len("modules/"):]
        m = re.match(r"(woz_[a-z_]+)/src/([a-z_]+)", d)
        if m:
            return f"{m.group(1)}/{m.group(2)}"
        return d.split("/")[0]
    if d.startswith("ports/"):
        return "/".join(d.split("/")[:2])
    return d.split("/")[0]


def mine_nodes() -> tuple[list[dict], list[dict]]:
    """Every architecture page becomes a node; its depends-on row, edges."""
    by_md: dict[str, int] = {}
    nodes: list[dict] = []
    pages = sorted(p for p in ARCH.glob("*/*.md") if p.name != "README.md")
    for p in pages:
        raw = p.read_text()
        m = H1.search(raw)
        if not m:
            continue
        path = m.group(1)
        body = raw[m.end():]
        para = []
        for line in body.splitlines():
            line = line.strip()
            if line.startswith(("```", "**", "#")):
                break
            if not line:
                if para:
                    break
                continue
            para.append(line)
        blurb = MD_LINK.sub(r"\1", " ".join(para)).replace("`", "")
        by_md[f"{p.parent.name}/{p.name}"] = len(nodes)
        nodes.append({
            "id": path,
            "name": path.rsplit("/", 1)[-1],
            "grp": cluster(path),
            "slug": f"{p.parent.name}.{p.stem}.html",
            "blurb": blurb,
            "_md": p,
        })

    links: list[dict] = []
    seen = set()
    for n in nodes:
        md = n.pop("_md")
        m = DEPS.search(md.read_text())
        if not m:
            continue
        for target in LINK.findall(m.group(1).split("**discussed in**")[0]):
            t = target.lstrip("./")
            key = t if "/" in t else f"{md.parent.name}/{t}"
            key = key.split("/")[-2] + "/" + key.split("/")[-1]
            if key in by_md and (n["id"], key) not in seen:
                seen.add((n["id"], key))
                links.append({"source": n["id"], "target": nodes[by_md[key]]["id"]})
    return nodes, links


def mine_symbols() -> dict[str, list[list[str]]]:
    """slug -> [[kind, name, anchor-url], ...] from the search index."""
    nav = (SITE / "nav.js").read_text()
    m = re.search(r'"?search"?\s*[:=]\s*\[', nav)
    if not m:
        return {}
    i = nav.index("[", m.end() - 1)
    depth = 0
    for j in range(i, len(nav)):
        depth += {"[": 1, "]": -1}.get(nav[j], 0)
        if depth == 0:
            break
    out: dict[str, list[list[str]]] = {}
    for kind, name, _ctx, url in json.loads(nav[i:j + 1]):
        if kind in ("function", "class", "macro") and "#" in url:
            out.setdefault(url.split("#")[0], []).append([kind[0], name, url])
    return out


def slot_map(nodes: list[dict]) -> dict[str, int]:
    """The 2D graphs' cluster slots, extended over every remaining directory."""
    slots: dict[str, int] = {}
    arch = SITE / "architecture.html"
    if arch.is_file():
        m = re.search(r'id="gv-slots">({[^<]*})</script>', arch.read_text())
        if m:
            slots = json.loads(m.group(1))
    nxt = max(slots.values(), default=-1) + 1
    for g in sorted({n["grp"] for n in nodes}):
        if g not in slots:
            slots[g] = nxt % len(COLORS)
            nxt += 1
    return slots


def ensure_lib() -> bool:
    """Vendor copy into site/, fetching it into internal/vendor once."""
    if not VENDOR.is_file():
        VENDOR.parent.mkdir(parents=True, exist_ok=True)
        try:
            with urllib.request.urlopen(LIB_URL, timeout=60) as r:
                VENDOR.write_bytes(r.read())
        except OSError as e:
            print(f"    3d renderer not vendored and not fetchable ({e}) — "
                  "skipping the 3D view", file=sys.stderr)
            return False
    shutil.copyfile(VENDOR, SITE / LIB)
    return True


def cta() -> str:
    return f"""<div class="gv3d" {CTA_MARK}><div>
<b>Fly through the whole surface</b>
<span>Every file the docs cover, in one 3D graph — orbit it, filter it, click a node for what it does.</span></div>
<a class="btn btn-primary" href="graph3d.html">Open the 3D view</a></div>
<style>.gv3d{{display:flex;align-items:center;gap:1.2rem;justify-content:space-between;flex-wrap:wrap;
margin:1.4rem 0 1.9rem;padding:1.05rem 1.25rem;border:1px solid var(--hairline);border-radius:16px;
background:linear-gradient(135deg,var(--tint),transparent 65%)}}
.gv3d b{{display:block;font-weight:650;color:var(--strong)}}
.gv3d span{{font-size:.88rem;color:var(--muted)}}
.gv3d .btn{{flex:none}}</style>
"""


def page(nodes, links, syms, slots) -> str:
    data = json.dumps({
        "nodes": [{k: n[k] for k in ("id", "name", "grp", "slug", "blurb")}
                  for n in nodes],
        "links": links,
        "syms": syms,
        "slots": slots,
        "colors": COLORS,
    }, separators=(",", ":"))
    tpl = Path(__file__).with_name("docs_3d_page.html").read_text()
    return tpl.replace("@@DATA@@", data).replace("@@LIB@@", LIB)


def main() -> int:
    if not (SITE / "index.html").is_file() or not ARCH.is_dir():
        print("    no rendered site — nothing to build")
        return 0
    if not ensure_lib():
        return 0

    nodes, links = mine_nodes()
    syms = mine_symbols()
    slots = slot_map(nodes)
    PAGE.write_text(page(nodes, links, syms, slots))
    print(f"    graph3d.html: {len(nodes)} nodes, {len(links)} edges, "
          f"symbols on {len(syms)} page(s)")

    arch = SITE / "architecture.html"
    if arch.is_file():
        raw = arch.read_text()
        if CTA_MARK in raw:
            print("    3D entry already present")
        elif CTA_ANCHOR in raw:
            arch.write_text(raw.replace(CTA_ANCHOR, cta() + CTA_ANCHOR, 1))
            print("    3D entry injected into architecture.html")
    return 0


if __name__ == "__main__":
    sys.exit(main())
