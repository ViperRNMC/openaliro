/* DW3000 (DWM3000EVB) wiring on ESP32-S3, SPI2/FSPI. Source of truth for the
 * wiring table in docs/esp32-bringup.md. Change to match how the DWM3000EVB is
 * soldered to your board. */
#ifndef WOZ_ESP_BOARD_PINS_H
#define WOZ_ESP_BOARD_PINS_H

#include "driver/spi_master.h"

#define WOZ_DW3000_SPI_HOST   SPI2_HOST

#define WOZ_DW3000_PIN_SCLK   12
#define WOZ_DW3000_PIN_MOSI   11
#define WOZ_DW3000_PIN_MISO   13
#define WOZ_DW3000_PIN_CS     10
#define WOZ_DW3000_PIN_RST     4
#define WOZ_DW3000_PIN_IRQ     5
#define WOZ_DW3000_PIN_WAKEUP  6

/* 2 MHz for init, 8 MHz steady (proven safe with a seated DWM3000EVB; matches
 * the nRF overlay). Raise once wiring is confirmed clean. */
#define WOZ_DW3000_SPI_SLOW_HZ 2000000
#define WOZ_DW3000_SPI_FAST_HZ 8000000

#endif /* WOZ_ESP_BOARD_PINS_H */
