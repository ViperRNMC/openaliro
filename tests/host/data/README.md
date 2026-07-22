# Aliro Lab captures

Serial logs consumed by `tools/aliro_lab.py`.

- `aliro_lab_sample.log` — a synthetic fixture with deterministic timings. The
  unit tests assert exact phase offsets against it, so treat it as a golden
  file: do not edit it casually.
- `aliro_lab_capture.log` — a real reader serial capture (present once one is
  committed). The suite only smoke-checks it — it parses, drives a bolt, and
  scores no FAIL — so real-world timing jitter will not break `make test`.

## Capturing a real one

Instrument and flash the **matter-lock** app (it stamps the full walk-up through
the bolt), then tee a serial session to a file and do an iPhone walk-up:

```
cd ports/esp32/apps/matter-lock
make menuconfig        # Aliro reader -> [*] Aliro Lab transaction trace
make flash
make term LOG=walkup.log
# walk up to the reader with the provisioned iPhone, let it unlock, walk away
# ctrl-t q to quit tio

python3 ../../../tools/aliro_lab.py walkup.log       # eyeball the report
cp walkup.log ../../../tests/host/data/aliro_lab_capture.log   # keep it
```

Turn the trace back off (`make menuconfig`, clear the option) before a
production build — it is default-off by design.
