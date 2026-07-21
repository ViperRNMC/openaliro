# Releasing

How to cut a release. Versions follow SemVer (`vMAJOR.MINOR.PATCH`); pre-1.0, a minor
bump means new capability and a patch bump means fixes only.

## Preconditions

1. `main` is green across all CI workflows, including `firmware-builds`: the release
   build reuses its containers and scripts, so a green gate there predicts a green
   release build.
2. The working tree is clean and on the release commit.
3. The [hardware validation checklist](hardware-validation.md) passes on that commit,
   with the results table filled in.

## Steps

1. **Changelog.** Move the `[Unreleased]` content in `CHANGELOG.md` under a new
   `## [X.Y.Z] - YYYY-MM-DD` heading, leave an empty `[Unreleased]` section, and update
   the link references at the bottom. Commit.
2. **Dry run (recommended).** Trigger the `release` workflow manually
   (`workflow_dispatch`) from the release branch. It exercises the whole pipeline and
   leaves the bundles as run artifacts without publishing anything.
3. **Tag.** `git tag -a vX.Y.Z -m "vX.Y.Z"` on the release commit, then push the branch
   and the tag. The tag push triggers the `release` workflow, which cold-builds both
   flash bundles (sources in `release/<target>/`), zips them with a `SHA256SUMS.txt`,
   and creates the GitHub release with those assets.
4. **Release notes.** Edit the created release and append, above the generated bundle
   table: the changelog section for this version, and the hardware validation results
   tables (with firmware commit, toolchain versions, phone model, and iOS version).

## Documentation

`make docs` renders the site into `site/` (gitignored). It has two halves:

- The **reference tree** (`site/api/`) is built by Doxygen from `docs/Doxyfile`. It needs
  `doxygen` and `graphviz` and nothing else, so it builds anywhere.
- The **subsystem tree, guides and site shell** come from a page generator kept outside
  this repository. `docs.sh` invokes it through an executable hook at
  `tools/docs_generate.local`, which is gitignored; override the path with `PAGE_GEN`.
  The hook takes one argument, `build` or `check`.

Without that hook `make docs` still succeeds and builds the reference tree over the
committed `docs/` tree, so a contributor never needs it. Regenerating `docs/` does.

## Notes

- Release builds are deliberately cache-free, unlike `firmware-builds`: a shipped
  binary must not depend on restored CI state. Cold builds are slow (the NCS workspace
  fetch alone is ~6.5 GB); the job timeouts allow 3 h (nRF) and 4 h (ESP32).
- The toolchain pins live in the workflow itself: the NCS toolchain container by
  digest for the nRF bundle, and the ESP-IDF container digest plus a bench-validated
  esp-matter revision (`ESP_MATTER_REV`) for the ESP32 bundle.
- The bundles cover the default configurations only: nRF5340 DK (DW3110, ST25R300)
  and the ESP32-S3 Matter lock. Variants (`CHIP=dw3720`, `HA=1`, the bench reader
  app) build from source.
