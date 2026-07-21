#!/usr/bin/env python3
"""Render a release FLASH.md into a self-contained FLASH.html.

The markdown file stays the single source of truth; this wraps its rendered
body in an embedded stylesheet (light + dark, no external assets) so the
bundle ships a guide that reads well in a browser. The output is committed
next to its source, so regenerate after editing a FLASH.md:

    pip install markdown==3.8
    python3 scripts/flash_html.py release/*/FLASH.md

Output is deterministic (no timestamps): it only changes when the source does.
"""

import pathlib
import sys

import markdown

STYLE = """
:root {
  color-scheme: light dark;
  --bg: #faf9f7;
  --panel: #f1efeb;
  --line: #dedad2;
  --ink: #201e1b;
  --muted: #6d6961;
  --accent: #0c6e63;
  --warn: #8a5a00;
  --warn-bg: #f6ecd9;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #191d1c;
    --panel: #202523;
    --line: #333a37;
    --ink: #e8e6e1;
    --muted: #969c96;
    --accent: #5cc4b4;
    --warn: #d9a84e;
    --warn-bg: #2a2620;
  }
}
* { box-sizing: border-box; }
html, body { margin: 0; background: var(--bg); }
body {
  color: var(--ink);
  font: 16px/1.65 ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
  padding: 3rem 1.25rem 4rem;
}
main { max-width: 44rem; margin: 0 auto; }
h1 {
  font-size: 1.7rem; line-height: 1.25; letter-spacing: -0.015em;
  margin: 0 0 1rem; text-wrap: balance;
}
h1 + p { color: var(--muted); }
h2 {
  font-size: 1.2rem; letter-spacing: -0.01em;
  margin: 2.75rem 0 0.75rem; padding-top: 1.5rem;
  border-top: 1px solid var(--line);
}
h3 { font-size: 1rem; margin: 1.75rem 0 0.5rem; }
p, ul, ol { margin: 0.75rem 0; }
li { margin: 0.3rem 0; }
li > p { margin: 0.3rem 0; }
a { color: var(--accent); text-decoration-thickness: 1px; text-underline-offset: 2px; }
strong { color: var(--ink); }
code {
  font: 0.85em/1.5 ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  background: var(--panel); border: 1px solid var(--line);
  border-radius: 4px; padding: 0.1em 0.35em;
}
pre {
  background: var(--panel); border: 1px solid var(--line); border-radius: 6px;
  padding: 0.85rem 1rem; overflow-x: auto; margin: 0.9rem 0;
}
pre code { background: none; border: 0; padding: 0; font-size: 0.85rem; }
table {
  border-collapse: collapse; width: 100%; margin: 1rem 0;
  font-size: 0.92rem; font-variant-numeric: tabular-nums;
}
th {
  text-align: left; font-size: 0.72rem; text-transform: uppercase;
  letter-spacing: 0.07em; color: var(--muted);
  border-bottom: 1px solid var(--ink); padding: 0.4rem 0.75rem 0.4rem 0;
}
td {
  border-bottom: 1px solid var(--line); padding: 0.5rem 0.75rem 0.5rem 0;
  vertical-align: top;
}
.tablewrap { overflow-x: auto; }
blockquote {
  margin: 1.25rem 0; padding: 0.75rem 1rem;
  background: var(--warn-bg); border-left: 3px solid var(--warn);
  border-radius: 0 6px 6px 0;
}
blockquote p { margin: 0.25rem 0; }
blockquote strong { color: var(--warn); }
footer {
  margin-top: 3.5rem; padding-top: 1rem; border-top: 1px solid var(--line);
  color: var(--muted); font-size: 0.8rem;
}
"""

TEMPLATE = """<!doctype html>
<!-- Generated from FLASH.md by scripts/flash_html.py; edit the .md, not this. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>{style}</style>
</head>
<body>
<main>
{body}
<footer>Rendered from the <code>FLASH.md</code> in this bundle; both carry the
same content. openaliro is evaluation firmware:
<a href="https://github.com/asxeem/openaliro">github.com/asxeem/openaliro</a></footer>
</main>
</body>
</html>
"""


def render(src: pathlib.Path) -> pathlib.Path:
    text = src.read_text(encoding="utf-8")
    title = next(
        (ln[2:].strip() for ln in text.splitlines() if ln.startswith("# ")),
        src.parent.name,
    )
    body = markdown.markdown(text, extensions=["tables", "fenced_code"])
    # Wide pin tables must scroll inside their own box on a phone.
    body = body.replace("<table>", '<div class="tablewrap"><table>')
    body = body.replace("</table>", "</table></div>")
    out = src.with_suffix(".html")
    out.write_text(
        TEMPLATE.format(title=title, style=STYLE, body=body), encoding="utf-8"
    )
    return out


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for arg in sys.argv[1:]:
        print(render(pathlib.Path(arg)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
