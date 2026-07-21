# Releasing

How to cut a release. Versions follow SemVer (`vMAJOR.MINOR.PATCH`); pre-1.0, a minor
bump means new capability and a patch bump means fixes only.

## Preconditions

1. `main` is green on all four CI workflows (host-tests, sanitizers, patch-drift,
   tooling).
2. The working tree is clean and on the release commit.
3. The [hardware validation checklist](hardware-validation.md) passes on that commit,
   with the results table filled in.

## Steps

1. **Changelog.** Move the `[Unreleased]` content in `CHANGELOG.md` under a new
   `## [X.Y.Z] - YYYY-MM-DD` heading, leave an empty `[Unreleased]` section, and update
   the link references at the bottom. Commit.
2. **Tag.** `git tag -a vX.Y.Z -m "vX.Y.Z"` on that commit, then push the branch and the
   tag.
3. **Artifact.** Build pristine from the tagged commit: `make rebuild`. Take
   `build/merged.hex` and compute its checksum:
   `shasum -a 256 build/merged.hex > merged.hex.sha256`.
4. **GitHub release.** Create a release from the tag. The notes contain, in order: the
   changelog section for this version, the hardware validation results tables (with
   firmware commit, toolchain versions, phone model, iOS version), and flashing
   instructions (`nrfjprog`/`west flash` of `merged.hex` needs a full-erase first flash,
   same as `make flash-erase`). Attach `merged.hex` and `merged.hex.sha256`.

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

- The artifact is built locally, not in CI: the NCS toolchain and workspace fetch
  (~6.5 GB) make a CI build slow and disk-tight on stock runners. If that changes, a
  tag-triggered build workflow can replace step 3.
- The prebuilt hex targets the default configuration (nRF5340 DK, DW3110, ST25R300).
  Other configurations (`CHIP=dw3720`) build from source.
- No ESP32 binary is attached. That build depends on an ESP-IDF and esp-matter pair that
  this repository does not pin, so a prebuilt image would not be reproducible from the
  tag alone. Build it from `ports/esp32/apps/matter-lock` and record the two toolchain versions in
  the release notes alongside the ESP32 validation table.
