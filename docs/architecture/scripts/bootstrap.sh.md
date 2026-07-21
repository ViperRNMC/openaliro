<!-- generated documentation — edit the source, not this file -->
# `scripts/bootstrap.sh`

bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream.
Fetches everything the build needs from public GitHub into ./workspace
(git-ignored), then applies our integration patches on top. It never reads from
any other local checkout — a clean upstream fetch every time.
Fetches (all public):
- Nordic add-on  ncs-door-lock-and-access-control @ the pin below
- NCS v3.3.0 + Zephyr + every module (via the add-on's own west manifest)
Prereq (once per machine): nRF Connect SDK v3.3.0 toolchain
nrfutil sdk-manager toolchain install --ncs-version v3.3.0
Usage:  scripts/bootstrap.sh                       # workspace in ./workspace
ALIRO_WS=/big/disk/ws scripts/bootstrap.sh # put the multi-GB workspace elsewhere

**discussed in** [`docs/protocol-notes.md`](../../protocol-notes.md), [`ports/nrf5340dk/README.md`](../../../ports/nrf5340dk/README.md)

## API

### `launch()`
`scripts/bootstrap.sh:29`

Launch the nRF Util SDK manager toolchain with the configured NCS version, passing through all remaining arguments.

### `apply_to()`
`scripts/bootstrap.sh:51`

Apply patch files to a repository, ensuring it is pristine (no uncommitted changes) before patching.
