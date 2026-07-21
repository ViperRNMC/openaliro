<!-- generated documentation — edit the source, not this file -->
# `tools/docs_3d.py`

Render the whole code surface as a flyable 3D graph: site/graph3d.html.

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

## API

### `cluster(path: str) -> str`
`tools/docs_3d.py:68`

Directory key for color grouping — same shape the 2D sidebar dots use.

**called by** `mine_nodes`

### `mine_nodes() -> tuple[list[dict], list[dict]]`
`tools/docs_3d.py:82`

Every architecture page becomes a node; its depends-on row, edges.

**called by** `main`  ·  **calls** `cluster`

### `mine_symbols() -> dict[str, list[list[str]]]`
`tools/docs_3d.py:132`

slug -> [[kind, name, anchor-url], ...] from the search index.

**called by** `main`

### `slot_map(nodes: list[dict]) -> dict[str, int]`
`tools/docs_3d.py:151`

The 2D graphs' cluster slots, extended over every remaining directory.

**called by** `main`

### `ensure_lib() -> bool`
`tools/docs_3d.py:167`

Vendor copy into site/, fetching it into internal/vendor once.

**called by** `main`

<details><summary>Undocumented (3)</summary>

- `cta`
- `page`
- `main`

</details>
