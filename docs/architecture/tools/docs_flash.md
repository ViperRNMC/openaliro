<!-- generated documentation — edit the source, not this file -->
# `tools/docs_flash.py`

Publish the browser flasher: site/flash/ = the web-flasher/ page + firmware.

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

**discussed in** [`web-flasher/README.md`](../../../web-flasher/README.md)

## API

### `repo_slug() -> str`
`tools/docs_flash.py:80`

owner/repo for the origin remote, or '' if none.

**called by** `main`

### `inject(page: Path, anchor: str, addition: str, before: bool) -> str`
`tools/docs_flash.py:102`

Insert addition next to anchor in page, once; report what happened.

**called by** `link_site`

<details><summary>Undocumented (3)</summary>

- `fetch`
- `link_site`
- `main`

</details>
