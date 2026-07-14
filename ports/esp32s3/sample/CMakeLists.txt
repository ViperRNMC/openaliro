# Minimal standalone Zephyr app that stands up the Woz UWB engine on ESP32-S3.
# Pulls in the engine + the ISC DW3000 driver as out-of-tree modules and applies
# the DW3000 devicetree overlay, then find_package(Zephyr) as usual.
cmake_minimum_required(VERSION 3.20.0)

# repo root = ports/esp32s3/sample -> ../../..
get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)

list(APPEND EXTRA_ZEPHYR_MODULES
  ${REPO_ROOT}/modules/woz_uwb
  ${REPO_ROOT}/deps/dw3000)

# DW3000 pins/side-band GPIOs for this board (edit ../dw3000.overlay to rewire).
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../dw3000.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(woz_esp32s3_ranging)

target_sources(app PRIVATE src/main.c)
