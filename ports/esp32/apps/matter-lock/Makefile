# Makefile — command entry point for the ESP32-S3 Aliro Matter door lock.
#
# Same shape as ../reader/Makefile, plus the esp-matter environment this
# project's CMakeLists requires (ESP_MATTER_PATH). Keeps the serial-port safety
# guard: flash/monitor auto-select the ESP's USB-UART port and REFUSE any
# SEGGER/J-Link port (VID 0x1366) so this can never touch the nRF5340DK rig.
#
#   make                 # this grouped, colourised help
#   make build           # idf.py build       -> build/door_lock.bin
#   make flash           # build + flash the ESP (safe port auto-detect)
#   make monitor         # interactive serial console (ctrl-] to quit)
#   make go              # build + flash + monitor, the usual bench loop
#   make ports           # list detected serial ports, labelled

.DEFAULT_GOAL := help

# ESP-IDF + esp-matter environment. Override if installed elsewhere:
#   make build IDF_EXPORT=/path/to/esp-idf/export.sh
IDF_EXPORT ?= $(HOME)/esp/esp-idf/export.sh
ESP_MATTER_PATH ?= $(HOME)/esp/esp-matter
# Chip target for `make set-target` (run once on a fresh checkout).
TARGET ?= esp32s3
# esp-matter's export.sh sets ESP_MATTER_PATH + the chip toolchain vars. Both
# exports are noisy on success, so their output is dropped; failures still show
# because the subsequent idf.py call fails loudly.
IDFPY := export ESP_MATTER_PATH="$(ESP_MATTER_PATH)" && . "$(IDF_EXPORT)" >/dev/null 2>&1 && \
         . "$(ESP_MATTER_PATH)/export.sh" >/dev/null 2>&1 && idf.py

# Serial-port override (else auto-detected). BAUD is the console rate; LOG tees
# the tio session to a file (make term LOG=session.log).
PORT ?=
BAUD ?= 115200
LOG ?=
# make flash FORCE=1 stops whatever holds the UART (a monitor/tio) before flashing.
FORCE ?=

# USB vendor ids (decimal, as ioreg reports them) used to classify ports.
# Keep values trailing-whitespace-free: an inline comment would leak spaces into
# the value and break the awk string compares below.
# 0x1A86 WCH CH343/CH9102 UART bridge (the ESP devkit); 0x303A Espressif native
# USB (S3 USB-OTG port); 0x1366 SEGGER J-Link = the nRF5340DK, NEVER flash.
VID_WCH       := 6790
VID_ESPRESSIF := 12346
VID_SEGGER    := 4966

# Resolve $$port to a SAFE ESP port, or fail loudly. Refuses SEGGER ports.
define RESOLVE_PORT
port='$(PORT)'; \
table=$$(ioreg -l -w0 2>/dev/null | LC_ALL=C awk '/"idVendor"/{v=$$0;sub(/.*= /,"",v);vend=v} /"IOCalloutDevice"/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c);print vend"\t"c}'); \
if [ -n "$$port" ]; then \
	vend=$$(printf '%s\n' "$$table" | awk -F'\t' -v p="$$port" '$$2==p{print $$1}'); \
	if [ "$$vend" = "$(VID_SEGGER)" ]; then echo "  refusing $$port: SEGGER/J-Link (nRF) port — protected rig" >&2; exit 1; fi; \
else \
	port=$$(printf '%s\n' "$$table" | awk -F'\t' '$$1=="$(VID_WCH)"||$$1=="$(VID_ESPRESSIF)"{print $$2; exit}'); \
	if [ -z "$$port" ]; then \
		if printf '%s\n' "$$table" | awk -F'\t' '$$1=="$(VID_SEGGER)"{f=1} END{exit f?0:1}'; then \
			echo "  only nRF J-Link (SEGGER) ports present — plug the ESP into its UART port, or pass PORT=" >&2; \
		else \
			echo "  no ESP serial port found — plug it in or pass PORT=/dev/cu.usbmodemXXXX" >&2; \
		fi; \
		ls /dev/cu.usbmodem* 2>/dev/null | sed 's/^/    seen: /' >&2; \
		exit 1; \
	fi; \
fi
endef

# Flashing needs the UART exclusively. If a monitor/tio holds $$port, refuse with
# a clear message (FORCE=1 stops the holder first). Non-destructive by default.
define ENSURE_PORT_FREE
holder=$$(lsof -t "$$port" 2>/dev/null | tr '\n' ' '); \
if [ -n "$$holder" ]; then \
	if [ "$(FORCE)" = "1" ]; then \
		echo "  FORCE: stopping port holder(s): $$holder"; \
		kill $$holder 2>/dev/null || true; \
		sleep 1; \
	else \
		echo "  $$port is held by PID(s): $$holder" >&2; \
		echo "  quit it (idf.py monitor: ctrl-]  ·  tio: ctrl-t q) or run: make flash FORCE=1" >&2; \
		exit 1; \
	fi; \
fi
endef

.PHONY: help check-env env set-target build rebuild reconfigure menuconfig size flash app-flash flash-erase monitor go run term ports clean

check-env:
	@[ -f "$(IDF_EXPORT)" ] || { echo "  ESP-IDF export.sh not found at $(IDF_EXPORT)" >&2; \
	  echo "  set it: make <target> IDF_EXPORT=/path/to/esp-idf/export.sh" >&2; exit 1; }
	@[ -f "$(ESP_MATTER_PATH)/export.sh" ] || { echo "  esp-matter not found at $(ESP_MATTER_PATH)" >&2; \
	  echo "  set it: make <target> ESP_MATTER_PATH=/path/to/esp-matter" >&2; exit 1; }

##@ Setup
## set-target: select the chip — run once on a fresh checkout  ·  TARGET=esp32s3
##   Regenerates sdkconfig from sdkconfig.defaults for the chosen chip.
set-target: check-env
	@$(IDFPY) set-target $(TARGET)

## env: sanity-check the toolchain — sources the exports, prints idf.py + paths
##   Confirms the environment resolves without starting a build. Every target
##   sources the exports internally, so `idf.py: command not found` never applies.
env: check-env
	@$(IDFPY) --version
	@printf '  ESP_MATTER_PATH = %s\n  IDF_EXPORT      = %s\n  TARGET          = %s\n' '$(ESP_MATTER_PATH)' '$(IDF_EXPORT)' '$(TARGET)'

##@ Build
## build: compile the app            -> build/door_lock.bin
build: check-env
	@$(IDFPY) build

## rebuild: clean, then a full rebuild  -> build/door_lock.bin
rebuild: check-env
	@$(IDFPY) fullclean && $(IDFPY) build

## reconfigure: re-run cmake after editing CMakeLists / sdkconfig / adding files
reconfigure: check-env
	@$(IDFPY) reconfigure

## menuconfig: interactive sdkconfig editor
menuconfig: check-env
	@$(IDFPY) menuconfig

## size: binary size + partition headroom
size: check-env
	@$(IDFPY) size

##@ Flash  (safe port auto-detect · never a SEGGER/nRF port)
## flash: build + write bootloader + partition table + app
flash: check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  flashing $$port"; \
	$(IDFPY) -p "$$port" flash

## app-flash: build + write only the app  (fast iteration)
app-flash: check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  app-flashing $$port"; \
	$(IDFPY) -p "$$port" app-flash

## flash-erase: full chip erase, then flash  (drops commissioning + Aliro NVS)
flash-erase: check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  erasing + flashing $$port"; \
	$(IDFPY) -p "$$port" erase-flash && $(IDFPY) -p "$$port" flash

##@ Monitor
## monitor: interactive serial console (logs + shell)  ·  ctrl-] to quit
##   Override: make monitor PORT=/dev/cu.usbmodemXXXX BAUD=115200
monitor: check-env
	@$(RESOLVE_PORT); \
	echo "  monitoring $$port @ $(BAUD)  (ctrl-] quits)"; \
	$(IDFPY) -p "$$port" -b $(BAUD) monitor

## go: build + flash + monitor in one shot  (the usual bench loop)
go: check-env
	@$(RESOLVE_PORT); \
	$(ENSURE_PORT_FREE); \
	echo "  build + flash + monitor on $$port"; \
	$(IDFPY) -p "$$port" -b $(BAUD) flash monitor

## run: alias for `go` (build + flash + monitor, the usual bench loop)
run: go

## term: interactive serial console via tio
##   Simple + robust, no ESP backtrace decode (use `make monitor` for that).
##   tio holds the port, so quit it (ctrl-t q) before `make flash`.
##   Override: make term PORT=/dev/cu.usbmodemXXXX BAUD=115200 LOG=session.log
term:
	@command -v tio >/dev/null 2>&1 || { printf '  tio not found  ·  install: brew install tio\n' >&2; exit 1; }
	@$(RESOLVE_PORT); \
	logargs=; [ -n '$(LOG)' ] && logargs='-L --log-file $(LOG)'; \
	printf '  tio %s @ %s 8N1  ·  ctrl-t q quits  ·  release before make flash\n' "$$port" '$(BAUD)'; \
	exec tio -b $(BAUD) $$logargs "$$port"

##@ Housekeeping
## ports: list attached usbmodem ports, labelled by board
ports:
	@ioreg -l -w0 2>/dev/null | LC_ALL=C awk \
	  '/"idVendor"/{v=$$0;sub(/.*= /,"",v);vend=v} \
	   /"IOCalloutDevice"/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c); \
	     lbl="unknown ("vend")"; \
	     if(vend=="$(VID_WCH)")lbl="ESP  (WCH UART)  <- flash target"; \
	     else if(vend=="$(VID_ESPRESSIF)")lbl="ESP  (native USB) <- flash target"; \
	     else if(vend=="$(VID_SEGGER)")lbl="nRF J-Link  ** DO NOT FLASH **"; \
	     printf "  %-34s %s\n", c, lbl}' ; \
	ls /dev/cu.usbmodem* >/dev/null 2>&1 || echo "  (no usbmodem ports attached)"

## clean: remove the build directory
clean: check-env
	@$(IDFPY) fullclean 2>/dev/null || rm -rf build; echo "  cleaned build/"

## help: this grouped, colourised target list
help:
	@if [ -t 1 ] && [ -z "$$NO_COLOR" ]; then \
	  b=$$(printf '\033[1m'); c=$$(printf '\033[36m'); y=$$(printf '\033[1;33m'); d=$$(printf '\033[2m'); r=$$(printf '\033[0m'); \
	else b=; c=; y=; d=; r=; fi; \
	printf '\n  %sAliro Matter door lock%s  %s·  esp32s3 · ESP-IDF + esp-matter%s\n' "$$b" "$$r" "$$d" "$$r"; \
	awk -v c="$$c" -v y="$$y" -v d="$$d" -v r="$$r" \
	  '/^##@ / { printf "\n  %s%s%s\n", y, substr($$0,5), r; next } \
	   /^## [^ ]/ { s=substr($$0,4); i=index(s,": "); \
	     printf "    %s%-12s%s %s%s%s\n", c, substr(s,1,i-1), r, d, substr(s,i+2), r }' \
	  $(MAKEFILE_LIST); \
	printf '\n'
