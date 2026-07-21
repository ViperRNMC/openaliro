# Tests

Everything here runs on a laptop: plain C compiler, no NCS toolchain, no ESP-IDF, no
hardware. Hardware truth comes separately from the manual
[hardware validation checklist](../docs/hardware-validation.md).

| Command | What it runs | CI workflow |
|---|---|---|
| `make test` | [`host/`](host/): the KAT suite for the shared engine + Aliro core (574 assertions), compiled against the `WOZ_PORT_HOST` backend with the Zephyr shims in `host/shim/` | host-tests |
| `make coverage` | Same suite instrumented (clang), HTML report, enforced line-coverage floor | host-tests |
| `make test-san` | Same suite under ASan + UBSan | sanitizers |
| `make fuzz` | libFuzzer (CI) or corpus replay (macOS) on the wire-facing parsers | fuzz |
| `make cbmc` | CBMC bounded proofs of memory safety for the wire parsers | cbmc |
| `make verify` | All of the above host gates, sequential, fail-fast — the pre-PR sweep | — |
| `make test-port` | [`../ports/esp32/test/`](../ports/esp32/test): the ESP32 port suite (port headers, crypto KATs, codec, provisioning) | port-tests |
| `make test-ws` | [`tooling/ws_seed_test.sh`](tooling/ws_seed_test.sh): hermetic tests of per-worktree workspace seeding | tooling |
| `tooling/patch_drift_check.sh` | Verifies every nRF patch still applies to the pinned upstream revisions (sparse network fetch, no workspace) | patch-drift |

CI additionally runs shellcheck over every script (tooling) and clang-format over
`modules/` (format). None of the CI jobs builds firmware; firmware claims are gated on
the bench, not the runner.
