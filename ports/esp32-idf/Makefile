# Makefile — command entry point for the ESP32-S3 Aliro port (self-contained).
#
# Thin front over `idf.py`, plus a serial-port safety guard: the flash/monitor
# targets auto-select the ESP's USB-UART port and REFUSE any SEGGER/J-Link port
# (VID 0x1366) so this can never touch the nRF5340DK rig.
#
#   make                 # this grouped, colourised help
#   make build           # idf.py build       -> build/woz_uwb_esp32s3.bin
#   make flash           # build + flash the ESP (safe port auto-detect)
#   make monitor         # interactive serial console (ctrl-] to quit)
#   make ports           # list detected serial ports, labelled
#   make flash PORT=/dev/cu.usbmodemXXXX   # override the auto-detected port

.DEFAULT_GOAL := help

# ESP-IDF environment. Override if installed elsewhere:
#   make build IDF_EXPORT=/path/to/esp-idf/export.sh
IDF_EXPORT ?= $(HOME)/esp/esp-idf/export.sh
IDFPY := . "$(IDF_EXPORT)" >/dev/null 2>&1 && idf.py

# Serial-port override (else auto-detected). BAUD is the console rate; LOG tees
# the tio session to a file (make term LOG=session.log).
PORT ?=
BAUD ?= 115200
LOG ?=

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
table=$$(ioreg -l -w0 2>/dev/null | awk '/"idVendor"/{v=$$0;sub(/.*= /,"",v);vend=v} /"IOCalloutDevice"/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c);print vend"\t"c}'); \
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

.PHONY: help check-idf build menuconfig size flash app-flash flash-erase monitor term ports clean

check-idf:
	@[ -f "$(IDF_EXPORT)" ] || { echo "  ESP-IDF export.sh not found at $(IDF_EXPORT)" >&2; \
	  echo "  set it: make <target> IDF_EXPORT=/path/to/esp-idf/export.sh" >&2; exit 1; }

##@ Build
## build: compile the app            -> build/woz_uwb_esp32s3.bin
build: check-idf
	@$(IDFPY) build

## menuconfig: interactive sdkconfig editor
menuconfig: check-idf
	@$(IDFPY) menuconfig

## size: binary size + partition headroom
size: check-idf
	@$(IDFPY) size

##@ Flash  (safe port auto-detect · never a SEGGER/nRF port)
## flash: build + write bootloader + partition table + app
flash: check-idf
	@$(RESOLVE_PORT); \
	echo "  flashing $$port"; \
	. "$(IDF_EXPORT)" >/dev/null 2>&1 && idf.py -p "$$port" flash

## app-flash: build + write only the app  (fast iteration)
app-flash: check-idf
	@$(RESOLVE_PORT); \
	echo "  app-flashing $$port"; \
	. "$(IDF_EXPORT)" >/dev/null 2>&1 && idf.py -p "$$port" app-flash

## flash-erase: full chip erase, then flash  (after a partition change)
flash-erase: check-idf
	@$(RESOLVE_PORT); \
	echo "  erasing + flashing $$port"; \
	. "$(IDF_EXPORT)" >/dev/null 2>&1 && idf.py -p "$$port" erase-flash && idf.py -p "$$port" flash

##@ Monitor
## monitor: interactive serial console (logs + input)  ·  ctrl-] to quit
##   Override: make monitor PORT=/dev/cu.usbmodemXXXX BAUD=115200
monitor: check-idf
	@$(RESOLVE_PORT); \
	echo "  monitoring $$port @ $(BAUD)  (ctrl-] quits)"; \
	. "$(IDF_EXPORT)" >/dev/null 2>&1 && idf.py -p "$$port" -b $(BAUD) monitor

## term: interactive serial console via tio (like the nRF `make term`)
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
	@ioreg -l -w0 2>/dev/null | awk \
	  '/"idVendor"/{v=$$0;sub(/.*= /,"",v);vend=v} \
	   /"IOCalloutDevice"/&&/usbmodem/{c=$$0;sub(/.*= "/,"",c);sub(/".*/,"",c); \
	     lbl="unknown ("vend")"; \
	     if(vend=="$(VID_WCH)")lbl="ESP  (WCH UART)  <- flash target"; \
	     else if(vend=="$(VID_ESPRESSIF)")lbl="ESP  (native USB) <- flash target"; \
	     else if(vend=="$(VID_SEGGER)")lbl="nRF J-Link  ** DO NOT FLASH **"; \
	     printf "  %-34s %s\n", c, lbl}' ; \
	ls /dev/cu.usbmodem* >/dev/null 2>&1 || echo "  (no usbmodem ports attached)"

## clean: remove the build directory
clean: check-idf
	@$(IDFPY) fullclean 2>/dev/null || rm -rf build; echo "  cleaned build/"

## help: this grouped, colourised target list
help:
	@if [ -t 1 ] && [ -z "$$NO_COLOR" ]; then \
	  b=$$(printf '\033[1m'); c=$$(printf '\033[36m'); y=$$(printf '\033[1;33m'); d=$$(printf '\033[2m'); r=$$(printf '\033[0m'); \
	else b=; c=; y=; d=; r=; fi; \
	printf '\n  %sAliro ESP32-S3 port%s  %s·  esp32s3 · ESP-IDF + NimBLE%s\n' "$$b" "$$r" "$$d" "$$r"; \
	awk -v c="$$c" -v y="$$y" -v d="$$d" -v r="$$r" \
	  '/^##@ / { printf "\n  %s%s%s\n", y, substr($$0,5), r; next } \
	   /^## [^ ]/ { s=substr($$0,4); i=index(s,": "); \
	     printf "    %s%-12s%s %s%s%s\n", c, substr(s,1,i-1), r, d, substr(s,i+2), r }' \
	  $(MAKEFILE_LIST); \
	printf '\n'
