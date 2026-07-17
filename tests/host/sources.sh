# shellcheck shell=bash
# shellcheck disable=SC2034  # every list here is consumed by the sourcing scripts
# Shared source lists for the host test + coverage builds. Sourced by run.sh and
# coverage.sh; the caller must set $ROOT to the repo root.
#
# UNIT_SRCS  — our code under test (the coverage denominator).
# TEST_SRCS  — the harness + per-module suites + the host AES double.
# SHIM_SRCS  — non-inline shim definitions (STS register no-ops, RX stubs).
# See coverage.sh for what is deliberately excluded and why.

SRC="$ROOT/modules/woz_uwb/src"
SHIM="$ROOT/tests/host/shim"
HOST="$ROOT/tests/host"

UNIT_SRCS=(
	"$SRC/ccc/ccc_kdf.c"
	"$SRC/ccc/ccc_mac.c"
	"$SRC/ccc/ccc_session.c"
	"$SRC/ccc/ccc_shim.c"
	"$SRC/ccc/ccc_sts.c"
	"$SRC/aliro/aliro_uwb_msg_builder.c"
	"$SRC/aliro/aliro_uwb_msg_parser.c"
	"$SRC/aliro/aliro_uwb_adapter.c"
	"$SRC/aliro/aliro_uwb_msg.c"
	"$SRC/aliro/aliro_uwb_session.c"
	"$SRC/ccc/cherry_ccc_shim.c"
	"$SRC/ccc/ccc_shim_rx.c"
	"$SRC/fira/fira_session.c"
	"$SRC/facade/woz_uwb_facade.c"
)

TEST_SRCS=(
	"$HOST/aes_ref.c"
	"$HOST/test.c"
	"$HOST/test_main.c"
	"$HOST/test_ccc_kdf.c"
	"$HOST/test_ccc_mac.c"
	"$HOST/test_ccc_sts.c"
	"$HOST/test_ccc_shim.c"
	"$HOST/test_ccc_session.c"
	"$HOST/test_aliro_builder.c"
	"$HOST/test_aliro_parser.c"
	"$HOST/test_aliro_adapter.c"
	"$HOST/test_aliro_msg.c"
	"$HOST/test_aliro_session.c"
	"$HOST/test_cherry.c"
	"$HOST/test_fira.c"
	"$HOST/test_facade.c"
	"$HOST/test_prepoll_gate.c"
	"$HOST/test_prepoll_round.c"
)

SHIM_SRCS=(
	"$SHIM/shim.c"
	"$SHIM/dw_rx_stub.c"
)

# Include search path: shim first so <zephyr/...> resolves to the stubs.
INCS=(
	-I"$SHIM"
	-I"$HOST"
	-I"$SRC/ccc"
	-I"$SRC/driver"
	-I"$SRC/aliro"
	-I"$SRC/aliro/include"
	-I"$SRC/fira"
	-I"$SRC/facade"
)

# The Aliro path is Kconfig-gated in-tree; the normal build has it on.
# _DEFAULT_SOURCE: glibc hides clock_gettime/CLOCK_MONOTONIC under strict
# -std=c11 without it (feature_test_macros(7)); Darwin headers ignore it.
DEFS=(-DCONFIG_WOZ_ALIRO=1 -D_DEFAULT_SOURCE)
