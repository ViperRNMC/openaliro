# Makefile — single command entry point for the Aliro NFC + UWB firmware.
#
# Thin front door over ./bootstrap.sh and ./build.sh (which hold the real logic:
# preflight, pristine-vs-incremental signature detection, chip resolution), plus
# the host-side test/coverage targets (plain C, no NCS toolchain or hardware).
#
#   make                 # this grouped, colourised help
#   make build           # incremental build   -> build/merged.hex
#   make test            # host KAT test for the pure CCC core
#   make coverage        # line coverage of that core (+ HTML report)
#   make build PRETTY=1 CHIP=dw3720   # build options (build targets only)

.DEFAULT_GOAL := help

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Options forwarded to build.sh. Set on the command line: make build PRETTY=1
CHIP     ?=
PRETTY   ?=
PRISTINE ?=
SELFTEST ?=

# Assemble the env prefix from whichever options were set.
ENV := $(strip \
  $(if $(CHIP),UWB_CHIP=$(CHIP)) \
  $(if $(PRETTY),PRETTY=$(PRETTY)) \
  $(if $(PRISTINE),PRISTINE=$(PRISTINE)) \
  $(if $(SELFTEST),UWB_SELFTEST=$(SELFTEST)))

.PHONY: help bootstrap build rebuild pretty selftest test coverage flash flash-erase clean

##@ Setup
## bootstrap: fetch NCS v3.3.0 + add-on (~6.5 GB), apply patches  ·  first run only
bootstrap:
	@$(ENV) ./bootstrap.sh

##@ Build
## build: incremental build            -> build/merged.hex
##   Options: CHIP=dw3720 (default dw3000)  PRETTY=1  PRISTINE=1  SELFTEST=1
##   e.g.     make build PRETTY=1 CHIP=dw3720
build:
	@$(ENV) ./build.sh build

## rebuild: force clean pristine build
rebuild:
	@$(ENV) ./build.sh rebuild

## selftest: one-shot boot self-test (no iPhone)
selftest:
	@$(ENV) UWB_SELFTEST=1 ./build.sh build

## pretty: build with curated / quiet console
pretty:
	@$(ENV) PRETTY=1 ./build.sh build

##@ Test
## test: run the host test suite for our logic  (no NCS toolchain / hardware)
test:
	@$(REPO_ROOT)/tests/host/run.sh

## coverage: line coverage of the pure CCC core  ->  terminal table + HTML
##   Instrumented (clang source-based coverage); slower than `make test` and
##   rebuilt at -O0. Artifacts under build/coverage/ (html/index.html).
coverage:
	@$(REPO_ROOT)/tests/host/coverage.sh

##@ Flash
## flash: app-only flash
flash:
	@$(ENV) ./build.sh flash

## flash-erase: full erase + flash  ·  needed after a net-core change
flash-erase:
	@$(ENV) ./build.sh flash-erase

##@ Housekeeping
## clean: remove ./build
clean:
	@rm -rf build && printf '  removed ./build\n'

## help: this grouped, colourised target list
help:
	@if [ -t 1 ] && [ -z "$$NO_COLOR" ]; then \
	  b=$$(printf '\033[1m'); c=$$(printf '\033[36m'); y=$$(printf '\033[1;33m'); d=$$(printf '\033[2m'); r=$$(printf '\033[0m'); \
	else b=; c=; y=; d=; r=; fi; \
	printf '\n  %sAliro NFC + UWB firmware%s  %s·  nrf5340dk/nrf5340/cpuapp%s\n' "$$b" "$$r" "$$d" "$$r"; \
	awk -v c="$$c" -v y="$$y" -v d="$$d" -v r="$$r" \
	  '/^##@ / { printf "\n  %s%s%s\n", y, substr($$0,5), r; next } \
	   /^## [^ ]/ { s=substr($$0,4); i=index(s,": "); \
	     printf "    %s%-14s%s %s%s%s\n", c, substr(s,1,i-1), r, d, substr(s,i+2), r }' \
	  $(MAKEFILE_LIST); \
	printf '\n'
