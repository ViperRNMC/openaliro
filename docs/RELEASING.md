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
   changelog section for this version, the hardware validation results table (with
   firmware commit, NCS version, phone model, iOS version), and flashing instructions
   (`nrfjprog`/`west flash` of `merged.hex` needs a full-erase first flash, same as
   `make flash-erase`). Attach `merged.hex` and `merged.hex.sha256`.

## Notes

- The artifact is built locally, not in CI: the NCS toolchain and workspace fetch
  (~6.5 GB) make a CI build slow and disk-tight on stock runners. If that changes, a
  tag-triggered build workflow can replace step 3.
- The prebuilt hex targets the default configuration (nRF5340 DK, DW3110, ST25R300).
  Other configurations (`CHIP=dw3720`) build from source.
