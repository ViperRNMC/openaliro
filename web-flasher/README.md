# web-flasher: browser flashing for the ESP32 Matter lock (S3 and C5)

A static page that flashes the merged lock image over WebSerial with
[ESP Web Tools](https://esphome.github.io/esp-web-tools/): plug in the board,
click Install, boot a working lock. One manifest carries a build per chip
(`openaliro-matter-lock.bin` for the ESP32-S3,
`openaliro-matter-lock-esp32c5.bin` for the ESP32-C5); ESP Web Tools flashes
the one matching the connected chip, and the page's Board dropdown can pin a
chip explicitly (it serves the button a single-build manifest built
client-side, so a mismatched board fails loudly). No ESP-IDF, no esptool, no drivers beyond
the browser. Chrome or Edge on a computer; WebSerial does not exist in Safari
or Firefox. The nRF5340 DK target stays out of scope: it programs over a
J-Link probe, which no browser API reaches.

## How the pieces fit

| Piece | Where | Job |
|---|---|---|
| `index.html` | committed here | the page: install button, wiring, first-boot checklist |
| `manifest.json` | committed here | ESP Web Tools manifest: one merged image per chip at offset 0, version `dev` |
| merged firmware | never committed | CI builds it (`idf.py merge-bin` in release.yml); locally `idf.py merge-bin` in the app dir |
| `tools/docs_flash.py` | committed | stages page + firmware into `site/flash/` during `make docs` |

The merged image is flashed at offset 0x0 and contains the bootloader (0x0 on
the S3), the partition table (0xC000, `CONFIG_PARTITION_TABLE_OFFSET`), and
the app (`ota_0` at 0x20000). Offsets come from
`ports/esp32/apps/matter-lock/partitions.csv` and were cross-checked against
ESP-IDF's own `gen_esp32part.py`; the manifest never encodes them because the
single-part-at-zero form makes them the image's internal business.

## Why the firmware is staged, not linked

GitHub release assets are served without CORS headers (probed 2026-07-22:
neither the `github.com` redirect nor its CDN answers
`Access-Control-Allow-Origin`), so a browser page cannot fetch them. The
firmware must sit next to the page on the same origin. `tools/docs_flash.py`
stages it at site-build time, preferring in order:

1. `web-flasher/openaliro-matter-lock.bin` (gitignored): a local
   `idf.py merge-bin` output, published with the committed `dev` manifest.
   A sibling `openaliro-matter-lock-esp32c5.bin` rides along when present.
2. The latest GitHub release's loose assets (`openaliro-matter-lock.bin`,
   `openaliro-matter-lock-esp32c5.bin` and
   `openaliro-matter-lock.manifest.json`, uploaded by release.yml), fetched
   server side where CORS does not apply.

Either way `tools/docs_flash.py` prunes manifest builds whose image was not
staged (an older release without the C5 asset, a bench run that merged only
one target), so the page never offers a firmware that would 404.
3. Neither: the flash page is skipped, loudly. Before the first release that
   is the normal state of a fresh checkout.

## Trying it locally (BENCH-GATED)

The page has been built and dry-checked but a real WebSerial flash on
hardware has not been run yet. To bench it:

```bash
cd ports/esp32/apps/matter-lock
idf.py set-target esp32s3 && idf.py build
idf.py merge-bin -o openaliro-matter-lock.bin
cp build/openaliro-matter-lock.bin ../../../../web-flasher/
# ESP32-C5 variant, own build dir so the S3 build survives:
#   idf.py -B build-c5 -D SDKCONFIG=build-c5/sdkconfig set-target esp32c5
#   idf.py -B build-c5 -D SDKCONFIG=build-c5/sdkconfig build merge-bin \
#     -o openaliro-matter-lock-esp32c5.bin
#   cp build-c5/openaliro-matter-lock-esp32c5.bin ../../../../web-flasher/
cd ../../../../web-flasher
python3 -m http.server 8000
```

Open `http://localhost:8000` in Chrome: localhost counts as a secure context,
so WebSerial works without https.
