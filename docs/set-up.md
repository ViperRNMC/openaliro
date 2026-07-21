# Installing

Install per target. The nRF5340 DK is the primary target. The ESP32-S3 apps
are ports of the same engine. Nothing needs hardware until you flash.

## nRF5340 DK (primary target)

Two commands. One per machine, one per checkout:

```bash
nrfutil sdk-manager toolchain install --ncs-version v3.3.0
make bootstrap
```

The first command runs once per machine. `make bootstrap` fetches NCS v3.3.0
plus the Nordic door-lock add-on, then applies this repo's patches: about
6.5 GB, into `./workspace` (git-ignored). Knobs:

| Knob | Effect |
|---|---|
| `ALIRO_WS=/big/disk/ws` | put the workspace somewhere else |
| `ALIRO_SHALLOW=1` | shallow fetch, saves several GB (what CI uses) |
| `ALIRO_TOOLCHAIN=env` | use the toolchain already on `PATH` |
| `HA=1` | Home Assistant patches (pair with `make build HA=1`) |

Then:

```bash
make build
make flash-erase
make term
```

The image lands in `./build/merged.hex`. The first flash needs the erase;
plain `make flash` after. `make term` opens the serial console.

Build options live in [configuring.md](configuring.md). Board setup lives in
[nrf5340-bringup.md](nrf5340-bringup.md).

## Host tests (no toolchain)

A plain C compiler is enough. Runs in about a second:

```bash
make test
make coverage
```

## ESP32-S3 ports

Both apps expect ESP-IDF at `~/esp/esp-idf`. Override with
`IDF_EXPORT=/path/to/esp-idf/export.sh`. CI uses ESP-IDF v5.5.4 and the
esp-matter revision pinned in
[`firmware-builds.yml`](../.github/workflows/firmware-builds.yml).

**Reader** (`../ports/esp32/apps/reader`). Plain ESP-IDF, no esp-matter:

```bash
cd ports/esp32/apps/reader
make set-target
make build
make flash
```

`make set-target` runs once per checkout.

**Matter door lock** (`../ports/esp32/apps/matter-lock`). Also needs
esp-matter at `~/esp/esp-matter` (override: `ESP_MATTER_PATH=`):

```bash
cd ports/esp32/apps/matter-lock
make set-target
make go
```

`make go` builds, flashes and opens the monitor. `flash` and `monitor`
refuse SEGGER/J-Link ports. They can never write to an
nRF5340 DK on the same bench.

Wiring: [esp32-bringup.md](esp32-bringup.md). Traps:
[esp32-gotchas.md](esp32-gotchas.md).

## Documentation site

```bash
brew install doxygen graphviz
make docs
```

The site lands in `./site`.

## If something fails

See [troubleshooting.md](troubleshooting.md).
