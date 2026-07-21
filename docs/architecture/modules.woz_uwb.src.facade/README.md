<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/`

| subsystem | about |
|---|---|
| [`modules/woz_uwb/src/facade/trace.h`](trace.h.md) | @file trace.h — Structured [WOZ_TRACE] emit helpers, gated on CONFIG_WOZ_E2E_TRACE. |
| [`modules/woz_uwb/src/facade/woz_alloc.h`](woz_alloc.h.md) | Memory allocation and timing facade: qmalloc, qcalloc, qfree wrap the platform heap; |
| [`modules/woz_uwb/src/facade/woz_bytes.h`](woz_bytes.h.md) | *first commit: "port: replace the Zephyr compat shims with a neutral woz_port.h contract"* |
| [`modules/woz_uwb/src/facade/woz_diag.h`](woz_diag.h.md) | @file woz_diag.h — DIAGK(): compile-time gate for verbose UWB bring-up diagnostics. |
| [`modules/woz_uwb/src/facade/woz_logfmt.c`](woz_logfmt.c.md) | @file woz_logfmt.c — PRETTY-gated high-res timestamp + compact colored log line. |
| [`modules/woz_uwb/src/facade/woz_logquiet.c`](woz_logquiet.c.md) | @file woz_logquiet.c — PRETTY-gated runtime muting of benign upstream error spam. |
| [`modules/woz_uwb/src/facade/woz_util.h`](woz_util.h.md) | *first commit: "port: replace the Zephyr compat shims with a neutral woz_port.h contract"* |
| [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](woz_uwb_facade.c.md) | UWB facade: binds the CCC credential-based STS engine to the DW3000 radio, exposes Aliro DS-TWR |
| [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](woz_uwb_facade.h.md) | Public header for UWB facade: exposes Aliro DS-TWR responder lifecycle and range query; the CCC |
