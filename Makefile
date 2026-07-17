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
STRICT   ?=

# Serial monitor (make term). PORT auto-detects the nRF5340DK console (VCOM1 —
# this firmware's console + Zephyr shell live there; VCOM0 is silent). Override
# PORT/BAUD if auto-detect picks the wrong board; set LOG=file.log to also tee
# the raw stream to a file.
PORT     ?=
BAUD     ?= 115200
LOG      ?=

# Assemble the env prefix from whichever options were set.
ENV := $(strip \
  $(if $(CHIP),UWB_CHIP=$(CHIP)) \
  $(if $(PRETTY),PRETTY=$(PRETTY)) \
  $(if $(PRISTINE),PRISTINE=$(PRISTINE)) \
  $(if $(SELFTEST),UWB_SELFTEST=$(SELFTEST)) \
  $(if $(STRICT),STRICT=$(STRICT)))

.PHONY: help bootstrap ws-seed ws-clean build rebuild pretty selftest test test-san coverage test-ws flash flash-erase term clean

##@ Setup
## bootstrap: fetch NCS v3.3.0 + add-on (~6.5 GB), apply patches  ·  first run only
bootstrap:
	@$(ENV) ./bootstrap.sh

## ws-seed: give THIS worktree its own workspace (APFS COW clone, ~0 disk)
##   Idempotent. Isolates worktrees so branch-bouncing can't build stale patches.
ws-seed:
	@$(REPO_ROOT)/ws-seed.sh

##@ Build
## build: incremental build            -> build/merged.hex
##   Options: CHIP=dw3720 (default dw3000)  PRETTY=1  PRISTINE=1  SELFTEST=1
##            STRICT=1 (drop suspect ranges)
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

## test-san: host suite rebuilt under ASan + UBSan  ·  memory-bug gate
test-san:
	@SAN=1 $(REPO_ROOT)/tests/host/run.sh

## test-ws: hermetic tests for per-worktree workspace auto-seeding
##   Runs in a temp dir with a stub bootstrap — no west, no hardware, and it
##   never touches this repo's own workspace/ or build/.
test-ws:
	@$(REPO_ROOT)/tests/tooling/ws_seed_test.sh

##@ Flash
## flash: app-only flash
flash:
	@$(ENV) ./build.sh flash

## flash-erase: full erase + flash  ·  needed after a net-core change
flash-erase:
	@$(ENV) ./build.sh flash-erase

##@ Monitor
## term: interactive serial console — live logs + typeable shell (tio, 115200 8N1)
##   Auto-detects the nRF5340DK console (VCOM1).  ctrl-t q quits.  Type `help` for shell commands.
##   Override: make term PORT=/dev/cu.usbmodemXXXX BAUD=115200 LOG=session.log
term:
	@command -v tio >/dev/null 2>&1 || { printf '  tio not found  ·  install: brew install tio\n' >&2; exit 1; }
	@port='$(PORT)'; \
	if [ -z "$$port" ]; then \
	  port=$$(ioreg -l -w0 2>/dev/null \
	    | awk '/kUSBSerialNumberString/{s=$$0;sub(/.*= "/,"",s);sub(/".*/,"",s);serial=s} /IOCalloutDevice/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c);print serial"\t"c}' \
	    | sort \
	    | awk -F'\t' '{cnt[$$1]++; if($$2>max[$$1])max[$$1]=$$2} END{best="";bc=-1; for(x in cnt) if(cnt[x]>bc||(cnt[x]==bc&&x<best)){bc=cnt[x];best=x} if(best!="")print max[best]}'); \
	fi; \
	if [ -z "$$port" ]; then \
	  printf '  no serial port found  ·  plug in the board or pass PORT=/dev/cu.usbmodemXXXX\n' >&2; \
	  ls /dev/cu.usbmodem* 2>/dev/null | sed 's/^/    candidate: /' >&2; \
	  exit 1; \
	fi; \
	logargs=; [ -n '$(LOG)' ] && logargs='-L --log-file $(LOG)'; \
	printf '  tio %s  @ %s 8N1  ·  logs + shell (type help)  ·  ctrl-t q to quit\n' "$$port" '$(BAUD)'; \
	exec tio -b $(BAUD) $$logargs "$$port"

##@ Housekeeping
## clean: remove ./build
clean:
	@rm -rf build && printf '  removed ./build\n'

## ws-clean: remove THIS worktree's local build + seeded workspace
##   Frees the per-worktree caches; re-seed with `make ws-seed`. A symlinked
##   workspace (pointing at the primary) is left alone — only a real local dir
##   is removed, never the shared source.
ws-clean:
	@rm -rf build
	@if [ -d workspace ] && [ ! -L workspace ]; then rm -rf workspace && printf '  removed ./build + ./workspace\n'; \
	else printf '  removed ./build (no local workspace to remove)\n'; fi

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
