# Contributing

Issues and PRs are welcome. This is a single-maintainer project, so response times are
best-effort; small, focused changes land fastest.

## Building and testing

Prerequisites and build steps are in the [README](README.md#getting-started). The short
version: `make bootstrap` once, then `make build`. Host-side tests need no toolchain or
hardware: `make test` runs the KAT suite, `make coverage` the coverage report.

Every PR must pass the CI gates that run automatically: host tests, coverage floor,
ASan/UBSan, patch drift, and shellcheck. Run `make test` locally before pushing.

## Ground rules

- **Never edit fetched upstream.** `workspace/` is fetched pristine and is layered onto,
  not modified: diffs go in `integration/patches/`, configuration in
  `integration/overlays/`, new code in `modules/` or `ports/`. A PR that edits fetched
  trees will be asked to rework.
- **Keep changes surgical.** Match the surrounding style; do not reformat or restructure
  code unrelated to your change.
- **Tests come with the change.** New parsing, session, or crypto code in
  `modules/woz_uwb/` needs host KAT coverage in `tests/host/` (CI enforces a coverage
  floor).
- **Hardware claims need hardware.** If a change affects on-air behavior, note in the PR
  what was validated on a DK + iPhone and what was not; see
  [`docs/hardware-validation.md`](docs/hardware-validation.md).

## Licensing

Project-original code is ISC (see [LICENSE](LICENSE)); the tree is mixed-license, with
per-file `SPDX-License-Identifier` headers as the source of truth. Contributions to
project-original files are accepted under ISC. Do not copy code in from external
projects unless its license is compatible and its header says where it came from.

## Security issues

Do not open public issues for vulnerabilities; see [SECURITY.md](SECURITY.md).
