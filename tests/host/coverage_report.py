#!/usr/bin/env python3
"""Pretty terminal coverage table from an llvm-cov `-summary-only` JSON export.

Usage: coverage_report.py <summary.json> [html_index_path]

Reads the summary llvm-cov emits, prints one aligned, colourised row per file
(lines covered, line %, function %, and a bar) plus a TOTAL, then the path to
the browsable HTML report. Colour is suppressed when stdout is not a TTY or
NO_COLOR is set, so it stays clean in logs and CI.
"""

import json
import os
import sys


def main() -> int:
    summary_path = sys.argv[1]
    html_path = sys.argv[2] if len(sys.argv) > 2 else None

    with open(summary_path) as fh:
        doc = json.load(fh)
    files = doc["data"][0]["files"]
    totals = doc["data"][0]["totals"]

    color = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
    if color:
        B, C, Y, D, R = "\033[1m", "\033[36m", "\033[1;33m", "\033[2m", "\033[0m"
        GRN, YEL, RED = "\033[32m", "\033[33m", "\033[31m"
    else:
        B = C = Y = D = R = GRN = YEL = RED = ""

    def tint(pct: float) -> str:
        return GRN if pct >= 90 else YEL if pct >= 75 else RED

    def bar(pct: float, width: int = 12) -> str:
        filled = int(round(pct / 100.0 * width))
        return "█" * filled + "░" * (width - filled)

    names = [os.path.basename(f["filename"]) for f in files]
    namew = max([len("file")] + [len(n) for n in names])

    def frac(cov: int, tot: int) -> str:
        return f"{cov}/{tot}"

    # widest "cov/tot" so the fraction column aligns.
    all_lines = [f["summary"]["lines"] for f in files] + [totals["lines"]]
    fracw = max(
        len("lines"), max(len(frac(l["covered"], l["count"])) for l in all_lines)
    )

    def row(label, label_col, lines, funcs, bold=False):
        pct = lines["percent"]
        t = tint(pct)
        lb = f"{B}{label}{R}" if bold else f"{C}{label}{R}"
        pctstr = f"{t}{B if bold else ''}{pct:5.1f}%{R}"
        return (
            f"    {lb}{' ' * (namew - len(label))}   "
            f"{frac(lines['covered'], lines['count']):>{fracw}}   "
            f"{pctstr}   {funcs['percent']:5.1f}%   {t}{bar(pct)}{R}"
        )

    print()
    print(f"  {B}Coverage{R}  {D}·  our code — host-testable logic{R}")
    print()
    print(
        f"    {D}{'file':<{namew}}   {'lines':>{fracw}}   {'cover':>6}   "
        f"{'funcs':>6}   {'':<12}{R}"
    )
    for f, n in zip(files, names):
        s = f["summary"]
        print(row(n, namew, s["lines"], s["functions"]))
    print(f"    {D}{'─' * (namew + fracw + 36)}{R}")
    print(row("TOTAL", namew, totals["lines"], totals["functions"], bold=True))
    print()
    if html_path:
        print(f"    {D}html:{R} {html_path}")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
