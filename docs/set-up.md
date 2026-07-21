# Installing

The nRF5340 DK is the primary target; the ESP32-S3 apps port the same engine.
No hardware needed until you flash.

## Get the code

```bash
git clone https://github.com/asxeem/openaliro.git
cd openaliro
```

Every command below runs from this directory.

## nRF5340 DK (primary target)

Two commands: the first once per machine, the second once per checkout.

```bash
nrfutil sdk-manager toolchain install --ncs-version v3.3.0
make bootstrap
```

`make bootstrap` pulls NCS v3.3.0 + the Nordic door-lock add-on (~6.5 GB)
into `./workspace` and applies this repo's patches. Knobs:

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

The image lands in `./build/merged.hex`; the first flash needs the erase,
plain `make flash` after.

Build options: [configuring.md](configuring.md). Board setup:
[nrf5340-bringup.md](nrf5340-bringup.md).

## Host tests (no toolchain)

Needs only a plain C compiler; runs in a second.

```bash
make test
make coverage
```

## ESP32-S3 ports

Both apps expect ESP-IDF at `~/esp/esp-idf` (override: `IDF_EXPORT=`); CI
pins ESP-IDF v5.5.4 and esp-matter in
[`firmware-builds.yml`](../.github/workflows/firmware-builds.yml).

**Reader** (`../ports/esp32/apps/reader`): plain ESP-IDF, no esp-matter.

```bash
cd ports/esp32/apps/reader
idf.py set-target esp32s3   # once per checkout, needs the IDF env exported
make build
make flash
```

**Matter door lock** (`../ports/esp32/apps/matter-lock`): also needs
esp-matter at `~/esp/esp-matter` (override: `ESP_MATTER_PATH=`).

```bash
cd ports/esp32/apps/matter-lock
make set-target
make go
```

`make go` = build + flash + monitor; `flash` and `monitor` refuse
SEGGER/J-Link ports, so they can never touch an nRF5340 DK on the bench.

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
