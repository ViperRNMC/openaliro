#!/usr/bin/env python3
"""Retheme the rendered site: warm paper surfaces, serif display headings.

The page generator ships a neutral blue-on-gray look. This pass restyles the
rendered output — never the generator — into the warm editorial style the
project wants: ivory paper backgrounds, near-black ink, a terracotta accent,
tan links in dark mode, and a serif display face over the headings. Two files
carry the whole theme:

  * site/style.css — every generated page links it, and every earlier pass
    styles its injections through the sheet's custom properties (--ground,
    --ink, --accent, …). Appending a redefinition of those properties at the
    end of the sheet wins the cascade everywhere at once, so the sidebar, the
    landing cards, the command chips and the search palette all follow without
    touching a single HTML file. A short component layer after the variables
    covers what variables cannot express: heading typefaces and the always-dark
    code panels.
  * site/api/doxygen-awesome.css — the reference tree's stylesheet exposes the
    same kind of seam (--page-background-color, --primary-color, …), so the
    API pages get the matching palette and headline face.

The display face is Source Serif 4 from Google Fonts, pulled with @import —
which CSS requires ahead of every rule, so the import is prepended while the
overrides are appended. Body text stays on the system sans stack.

Idempotent like the other passes: a marker comment guards both files, so
re-running over a kept site/ changes nothing. Run from the repo root, any time
after the generators; it edits only the two stylesheets, no page markup.
"""

from __future__ import annotations

import sys
from pathlib import Path

SITE = Path("site")
SHEET = SITE / "style.css"
API_SHEET = SITE / "api" / "doxygen-awesome.css"

MARK = "/* aliro-theme */"

FONT_IMPORT = (
    "@import url('https://fonts.googleapis.com/css2"
    "?family=Source+Serif+4:ital,opsz,wght@0,8..60,400..600;1,8..60,400..600"
    "&display=swap');\n"
)

# The four variable blocks mirror the base sheet's own scoping exactly:
# OS preference first, explicit toggle second, so the toggle keeps winning.
THEME = f"""
{MARK}
:root{{
  --ground:#faf9f5; --surface:#faf9f5; --raise:#f0eee6; --card:#ffffff;
  --ink:#3d3d3a; --strong:#141413; --muted:#73726c; --faint:#9c9a92;
  --line:rgba(31,30,29,.15); --hairline:rgba(31,30,29,.08);
  --accent:#c96442; --accent-ink:#141413;
  --tint:rgba(20,20,19,.07); --tint-line:rgba(31,30,29,.24);
  --shadow:0 1px 2px rgba(31,30,29,.04),0 12px 32px -20px rgba(31,30,29,.16);
  --shadow-lg:0 30px 70px -26px rgba(31,30,29,.35);
  --serif:"Source Serif 4",Georgia,"Times New Roman",serif;
  --codebg:#1f1e1d; --codeink:#dedcd3; --codeline:rgba(250,249,245,.09);
  --herotint:rgba(201,100,66,.055);
}}
@media (prefers-color-scheme:dark){{:root{{
  --ground:#1f1e1d; --surface:#1b1a19; --raise:#302f2c; --card:#262624;
  --ink:#c2c0b6; --strong:#faf9f5; --muted:#9e9c93; --faint:#6e6c64;
  --line:rgba(250,249,245,.13); --hairline:rgba(250,249,245,.07);
  --accent:#d97757; --accent-ink:#d4a27f;
  --tint:rgba(212,162,127,.13); --tint-line:rgba(212,162,127,.38);
  --shadow:0 1px 2px rgba(0,0,0,.5),0 14px 34px -16px rgba(0,0,0,.55);
  --shadow-lg:0 34px 80px -24px rgba(0,0,0,.7);
  --codebg:#171614; --codeink:#d6d3c9; --codeline:rgba(250,249,245,.07);
  --herotint:rgba(212,162,127,.05);
}}}}
:root[data-theme="light"]{{
  --ground:#faf9f5; --surface:#faf9f5; --raise:#f0eee6; --card:#ffffff;
  --ink:#3d3d3a; --strong:#141413; --muted:#73726c; --faint:#9c9a92;
  --line:rgba(31,30,29,.15); --hairline:rgba(31,30,29,.08);
  --accent:#c96442; --accent-ink:#141413;
  --tint:rgba(20,20,19,.07); --tint-line:rgba(31,30,29,.24);
  --shadow:0 1px 2px rgba(31,30,29,.04),0 12px 32px -20px rgba(31,30,29,.16);
  --shadow-lg:0 30px 70px -26px rgba(31,30,29,.35);
  --codebg:#1f1e1d; --codeink:#dedcd3; --codeline:rgba(250,249,245,.09);
  --herotint:rgba(201,100,66,.055);
}}
:root[data-theme="dark"]{{
  --ground:#1f1e1d; --surface:#1b1a19; --raise:#302f2c; --card:#262624;
  --ink:#c2c0b6; --strong:#faf9f5; --muted:#9e9c93; --faint:#6e6c64;
  --line:rgba(250,249,245,.13); --hairline:rgba(250,249,245,.07);
  --accent:#d97757; --accent-ink:#d4a27f;
  --tint:rgba(212,162,127,.13); --tint-line:rgba(212,162,127,.38);
  --shadow:0 1px 2px rgba(0,0,0,.5),0 14px 34px -16px rgba(0,0,0,.55);
  --shadow-lg:0 34px 80px -24px rgba(0,0,0,.7);
  --codebg:#171614; --codeink:#d6d3c9; --codeline:rgba(250,249,245,.07);
  --herotint:rgba(212,162,127,.05);
}}

/* Display headings. Direct children only: the nested h2s — the uppercase
   .section-h rails and the per-module .arch-sec headings — keep the sans
   they were designed around. The .mono variants outrank these on
   specificity, so API titles stay monospace untouched. */
.doc>h1,.doc>h2,.doc>h3,.hero-in h1,.hero h1{{font-family:var(--serif);letter-spacing:0}}
.doc>h1{{font-size:2.05rem;font-weight:430;line-height:1.22}}
.hero-in h1{{font-size:2.15rem;font-weight:430;line-height:1.22}}
.hero h1{{font-size:clamp(2.5rem,5vw,3.35rem);font-weight:420;line-height:1.1}}
.doc>h2{{font-size:1.42rem;font-weight:500}}
.doc>h3{{font-size:1.1rem;font-weight:550}}

/* Code panels stay dark on paper in both schemes; inline code keeps the
   light chip the base sheet gives it. */
pre{{background:var(--codebg);border-color:var(--codeline);color:var(--codeink)}}
.hero-band{{background:linear-gradient(180deg,var(--herotint),transparent 82%)}}
.term{{background:var(--codebg);border-color:var(--codeline)}}
.btn-primary:hover{{background:#a34f31;border-color:#a34f31}}
:root[data-theme="dark"] .btn-primary:hover{{background:var(--accent-ink);border-color:var(--accent-ink)}}
@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]) .btn-primary:hover{{background:var(--accent-ink);border-color:var(--accent-ink)}}}}

/* Readability: a larger, airier measure throughout. Bumping the root size
   scales every rem-based component with it, sidebar included. */
html{{font-size:17px}}
:root{{--article:48rem}}
.doc{{line-height:1.78}}
.doc p{{margin:1.05rem 0}}
.lede{{font-size:1.16rem;line-height:1.65}}
.doc>h1{{font-size:2.35rem}}
.hero-in h1{{font-size:2.45rem}}
.doc>h2{{font-size:1.6rem;margin:3rem 0 1rem}}
.doc>h3{{font-size:1.22rem;margin:2rem 0 .6rem}}
pre{{padding:1rem 1.15rem;font-size:.86rem;line-height:1.7}}
p code,li code,.chips code,.xref code,dd code{{padding:.12em .38em;border-radius:6px;font-size:.85em}}

/* Tables read like Claude's: horizontal hairlines only, a firmer header rule. */
.doc th,.doc td{{border:0;border-bottom:1px solid var(--hairline);padding:.6rem .8rem}}
.doc thead th{{border-bottom:1px solid var(--line);font-weight:600}}

/* Solid topbar: the translucent blur smeared over images as they scrolled
   underneath. The crumb drops the monospace for a quiet sans. */
.topbar{{background:var(--ground);backdrop-filter:none;-webkit-backdrop-filter:none;
  border-bottom:1px solid var(--hairline);height:3.4rem}}
.crumb{{font-family:var(--sans);font-size:.85rem;letter-spacing:0}}

/* Sidebar: calmer and more generous. Group caps become title-case headings;
   the parent cap above the bucket subcaps is redundant and goes; the
   coverage meter is build telemetry, not reader wayfinding. */
.cov{{display:none}}
.tree .tree-cap,.tree .tree-subcap{{font-size:.8rem;font-weight:650;letter-spacing:0;
  text-transform:capitalize;color:var(--strong);padding:1.5rem .6rem .45rem}}
.tree .tree-subcap{{text-transform:none}}
.tree .tree-cap:has(+ * .tree-subcap){{display:none}}
.doclink{{font-size:.9rem;padding:.5rem .7rem;gap:.6rem}}
.item-g{{font-size:.88rem;padding:.42rem .65rem}}
.side-head{{padding:1.35rem 1.2rem 1.1rem}}
.side .search-btn{{background:var(--ground);border-color:var(--line);color:var(--faint);
  font-weight:500;animation:none}}
.side .search-btn:hover{{border-color:var(--tint-line);color:var(--muted);box-shadow:none}}
.side .search-btn kbd{{color:var(--faint);border-color:var(--line);background:var(--surface)}}

/* Right rail: a serif "On this page", larger targets. */
.toc-cap{{font-family:var(--serif);font-size:1.02rem;font-weight:500;letter-spacing:0;
  text-transform:none;color:var(--strong);margin-bottom:.7rem}}
.toc-link{{font-size:.85rem;padding:.3rem .8rem}}

/* The landing pill reads as a Claude-style eyebrow, not a bordered chip,
   and the wordmark keeps its full display size against the wider measure. */
.hero .pill{{border:none;background:none;padding:0;color:var(--accent);
  font-size:.74rem;font-weight:650;letter-spacing:.13em;text-transform:uppercase}}
.hero-in .hero h1{{font-size:clamp(2.6rem,5vw,3.4rem)}}

/* Cards and the hero terminal: softer radii, tame scrollbars on dark panels. */
.feats a{{border-radius:16px}}
pre,.t-body{{scrollbar-width:thin;scrollbar-color:rgba(250,249,245,.18) transparent}}
pre::-webkit-scrollbar,.t-body::-webkit-scrollbar{{height:8px}}
pre::-webkit-scrollbar-thumb,.t-body::-webkit-scrollbar-thumb{{background:rgba(250,249,245,.16);border-radius:9px}}
"""

# doxygen-awesome scopes dark under html.dark-mode plus the OS preference;
# the reference tree is built with HTML_COLORSTYLE=LIGHT, so the dark blocks
# only matter if that ever changes — kept for parity, they cost nothing.
API_DARK = """
  --primary-color:#d4a27f; --primary-dark-color:#d97757; --primary-light-color:#e4bfa2;
  --page-background-color:#1f1e1d; --page-foreground-color:#c2c0b6;
  --page-secondary-foreground-color:#9e9c93;
  --separator-color:#3a3833; --side-nav-background:#1b1a19; --side-nav-foreground:#c2c0b6;
  --header-background:#1f1e1d; --header-foreground:#c2c0b6;
  --searchbar-background:#262624;
  --code-background:#302f2c; --code-foreground:#c2c0b6;
  --fragment-background:#171614; --fragment-foreground:#d6d3c9;
  --odd-color:rgba(250,249,245,.04);
  --tablehead-background:#262624;
"""

API_THEME = f"""
{MARK}
html{{
  --primary-color:#c96442; --primary-dark-color:#a34f31; --primary-light-color:#d97757;
  --page-background-color:#faf9f5; --page-foreground-color:#3d3d3a;
  --page-secondary-foreground-color:#73726c;
  --separator-color:#e6e4da; --side-nav-background:#f5f3ec; --side-nav-foreground:#3d3d3a;
  --header-background:#faf9f5; --header-foreground:#3d3d3a;
  --searchbar-background:#ffffff;
  --code-background:#f0eee6; --code-foreground:#3d3d3a;
  --fragment-background:#1f1e1d; --fragment-foreground:#dedcd3;
  --odd-color:rgba(31,30,29,.04);
  --tablehead-background:#f0eee6;
}}
html.dark-mode{{{API_DARK}}}
@media (prefers-color-scheme:dark){{html:not(.light-mode){{{API_DARK}}}}}
.title,h1,h2.groupheader{{font-family:"Source Serif 4",Georgia,serif;font-weight:450;letter-spacing:0}}
"""


def theme(sheet: Path, css: str) -> str:
    """Prepend the font import and append the overrides; report what happened."""
    raw = sheet.read_text()
    if MARK in raw:
        return "already themed"
    sheet.write_text(FONT_IMPORT + raw + css)
    return "themed"


def main() -> int:
    did = []
    if SHEET.is_file():
        did.append(f"site shell {theme(SHEET, THEME)}")
    if API_SHEET.is_file():
        did.append(f"reference tree {theme(API_SHEET, API_THEME)}")
    if not did:
        print("    no rendered site — nothing to theme")
        return 0
    print(f"    {', '.join(did)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
